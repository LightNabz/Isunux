#include "fat.h"
#include "ata.h"
#include "kutil.h"
#include "serial.h"

/* ---- on-disk layout, straight from the FAT spec ---- */

typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_descriptor;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
    uint8_t  boot_code[448];
    uint16_t signature; /* 0xAA55 */
} fat16_bpb_t;

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F /* READ_ONLY|HIDDEN|SYSTEM|VOLUME_ID combined -- marks a
                                  * long-filename entry, not a real directory entry */

#define FAT16_EOC_MIN 0xFFF8 /* cluster values >= this mean "end of chain" */

/* ---- cached volume geometry, filled in by fat_mount() ---- */

static int fat_mounted = 0;
static uint16_t bytes_per_sector;
static uint8_t  sectors_per_cluster;
static uint16_t root_entry_count;
static uint8_t  num_fats;
static uint16_t fat_size_16;
static uint32_t first_fat_sector;
static uint32_t first_root_dir_sector;
static uint32_t root_dir_sectors;
static uint32_t first_data_sector;

/* ---- per-node state, reached via vnode_t::priv (see fat.h's top
 * comment for why this isn't embedded-and-cast the way tmpfs does
 * it) ---- */
typedef struct {
    uint32_t first_cluster; /* 0 sentinel = FAT16 root directory: fixed sector range,
                              * not a cluster chain (see fat_iterate_dir). Cluster 0 is
                              * never a legal DATA cluster -- real chains start at 2 --
                              * so this is a safe, unambiguous sentinel. A genuine
                              * subdirectory always has a nonzero first_cluster in
                              * practice: mkfs.fat always gives one at least one cluster
                              * for its "." and ".." entries. A regular FILE can
                              * legitimately be 0 too, though -- a genuinely empty file
                              * that's never had a byte written to it. fat_file_write()
                              * is the only place that ever needs to tell those two
                              * "0" cases apart, and it can: it's simply never called on
                              * a directory (write is NULL in fat_dir_ops). */
    uint32_t file_size;     /* 0 for directories -- FAT has no meaningful "directory
                              * size"; matches every other filesystem here (tmpfs/devfs
                              * dirs report size 0 too, via vfs_stat_t's generic
                              * fallback when .stat is NULL, which fat_dir_ops uses). */
    uint32_t dirent_lba;    /* which sector this node's own 32-byte directory entry
                              * lives in on disk, and the byte offset within that
                              * sector -- set once, at lookup time (see
                              * fat_lookup_visitor), and used by fat_writeback_dirent()
                              * after every write to persist the (possibly new)
                              * first_cluster/file_size back to disk. Meaningless for
                              * the root -- the root has no dirent of its own anywhere
                              * (it's the fixed sector range, not an entry inside some
                              * other directory), but that's fine: nothing ever calls
                              * fat_file_write() on a directory in the first place. */
    uint32_t dirent_offset;
} fat_meta_t;

#define FAT_MAX_NODES 128
static vnode_t node_pool[FAT_MAX_NODES];
static fat_meta_t meta_pool[FAT_MAX_NODES];
static int node_count = 0;
static fat_meta_t root_meta; /* used directly on the caller-supplied mount-point vnode,
                               * never pool-allocated -- see fat_install() */

static vnode_t *fat_alloc_node(void) {
    if (node_count >= FAT_MAX_NODES) return NULL;
    int i = node_count++;
    k_memset(&node_pool[i], 0, sizeof(vnode_t));
    k_memset(&meta_pool[i], 0, sizeof(fat_meta_t));
    node_pool[i].priv = &meta_pool[i];
    return &node_pool[i];
}

static void set_name(vnode_t *v, const char *name) {
    uint64_t i = 0;
    for (; name[i] && i < VFS_MAX_NAME - 1; i++) v->name[i] = name[i];
    v->name[i] = '\0';
}

/* ---- cluster chain walking ---- */

static uint32_t cluster_to_lba(uint32_t cluster) {
    return first_data_sector + (cluster - 2) * sectors_per_cluster;
}

/* Looks up cluster `cluster`'s FAT table entry -- the next cluster in
 * its chain, or a value >= FAT16_EOC_MIN if this was the last one.
 * Re-reads the relevant FAT sector from disk on every call rather
 * than caching the whole table in RAM -- simplest thing that works,
 * matches the ATA driver's own "correctness first, polling is fine"
 * philosophy, and the FAT table for a disk this small costs nothing
 * to re-read a few extra times. Worth revisiting if a much bigger
 * disk ever makes this measurably slow. */
static uint32_t fat_next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 2; /* FAT16: 2 bytes per entry */
    uint32_t fat_sector = first_fat_sector + (fat_offset / bytes_per_sector);
    uint32_t entry_off = fat_offset % bytes_per_sector;

    uint8_t sector[512];
    if (ata_read_sectors(fat_sector, 1, sector) != 0) return 0xFFFFFFFF; /* IO error -- treat as end of chain */

    return (uint32_t)sector[entry_off] | ((uint32_t)sector[entry_off + 1] << 8);
}

/* ---- directory entry decoding ---- */

/* Converts an 11-byte 8.3 field ("HELLO   TXT") into a normal
 * lowercased display string ("hello.txt") -- see fat.h's top comment
 * for why lowercasing here is what lets lookup() reuse a plain
 * string compare instead of writing separate case-insensitive
 * matching. out must have room for at least 13 bytes ("12345678.123\0"). */
static void fat_decode_83_name(const uint8_t *entry, char *out) {
    int i = 0;
    for (int b = 0; b < 8 && entry[b] != ' '; b++) {
        char c = (char)entry[b];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[i++] = c;
    }
    if (entry[8] != ' ') {
        out[i++] = '.';
        for (int b = 8; b < 11 && entry[b] != ' '; b++) {
            char c = (char)entry[b];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            out[i++] = c;
        }
    }
    out[i] = '\0';
}

/* visitor return: 1 = stop iterating (found what you wanted), 0 = keep going.
 * real_index counts only entries the visitor actually got called for
 * (deleted/LFN/volume-label entries are skipped before it's ever
 * invoked and don't consume an index value) -- this is what makes
 * readdir()'s index argument line up with what a matching lookup()
 * would find. entry_lba/entry_offset locate the entry's own 32 bytes
 * on disk (which sector, and the byte offset within it) -- lookup
 * uses this to remember where to write a size/cluster update back to
 * later; readdir doesn't care and just ignores them. */
typedef int (*fat_dirent_visitor_t)(const char *display_name, const uint8_t *raw_entry,
                                     int real_index, uint32_t entry_lba, uint32_t entry_offset, void *ctx);

/* Scans one already-read 512-byte directory sector (which lives at
 * `sector_lba` on disk -- needed so the visitor can record exactly
 * where an entry it cares about came from). Sets *stop to 1 if the
 * true end-of-directory marker (name[0]==0x00) was hit, or the
 * visitor itself asked to stop -- either way the caller must not read
 * any further sectors after that. */
static void fat_scan_sector_dirents(const uint8_t *sector, uint32_t sector_lba, fat_dirent_visitor_t visitor,
                                     void *ctx, int *real_index, int *stop) {
    for (int off = 0; off < 512; off += 32) {
        const uint8_t *entry = sector + off;
        uint8_t first = entry[0];

        if (first == 0x00) { *stop = 1; return; } /* true end of directory -- nothing after this is valid */
        if (first == 0xE5) continue; /* deleted entry */

        uint8_t attr = entry[11];
        if (attr == FAT_ATTR_LFN) continue; /* long-filename entry -- not supported, see fat.h */
        if (attr & FAT_ATTR_VOLUME_ID) continue; /* volume label, not a real file/dir */

        char display[13];
        fat_decode_83_name(entry, display);

        if (visitor(display, entry, *real_index, sector_lba, (uint32_t)off, ctx)) { *stop = 1; return; }
        (*real_index)++;
    }
}

/* Walks every real entry in `dir_vnode` (root or subdirectory alike),
 * calling visitor for each. Handles the two genuinely different
 * on-disk shapes FAT16 has for "a directory": the root, which is a
 * fixed sector range (first_root_dir_sector .. +root_dir_sectors),
 * and everything else, which is an ordinary cluster chain read the
 * same way a file's data would be, just interpreted as 32-byte
 * dirents instead of raw bytes. */
static void fat_iterate_dir(vnode_t *dir_vnode, fat_dirent_visitor_t visitor, void *ctx) {
    fat_meta_t *meta = (fat_meta_t *)dir_vnode->priv;
    uint8_t sector[512];
    int real_index = 0;
    int stop = 0;

    if (meta->first_cluster == 0) {
        for (uint32_t s = 0; s < root_dir_sectors && !stop; s++) {
            uint32_t lba = first_root_dir_sector + s;
            if (ata_read_sectors(lba, 1, sector) != 0) return;
            fat_scan_sector_dirents(sector, lba, visitor, ctx, &real_index, &stop);
        }
        return;
    }

    uint32_t cluster = meta->first_cluster;
    while (cluster >= 2 && cluster < FAT16_EOC_MIN && !stop) {
        uint32_t cluster_lba = cluster_to_lba(cluster);
        for (uint8_t s = 0; s < sectors_per_cluster && !stop; s++) {
            uint32_t lba = cluster_lba + s;
            if (ata_read_sectors(lba, 1, sector) != 0) return;
            fat_scan_sector_dirents(sector, lba, visitor, ctx, &real_index, &stop);
        }
        if (!stop) cluster = fat_next_cluster(cluster);
    }
}

/* ---- vnode_ops ---- */

/* forward-declared so the visitor below can set node->ops directly --
 * real definitions are further down, after the functions they point to */
static vnode_ops_t fat_file_ops;
static vnode_ops_t fat_dir_ops;

typedef struct {
    const char *want_name;
    vnode_t *found;
    vnode_t *parent;
} fat_lookup_ctx_t;

static int fat_lookup_visitor(const char *display_name, const uint8_t *raw_entry, int real_index,
                               uint32_t entry_lba, uint32_t entry_offset, void *ctx_) {
    (void)real_index;
    fat_lookup_ctx_t *ctx = (fat_lookup_ctx_t *)ctx_;
    if (k_strcmp(display_name, ctx->want_name) != 0) return 0; /* keep looking */

    vnode_t *node = fat_alloc_node();
    if (!node) return 1; /* out of node slots -- stop, nothing more we can do */

    uint8_t attr = raw_entry[11];
    uint16_t cluster_lo = (uint16_t)raw_entry[26] | ((uint16_t)raw_entry[27] << 8);
    uint32_t size = (uint32_t)raw_entry[28] | ((uint32_t)raw_entry[29] << 8) |
                     ((uint32_t)raw_entry[30] << 16) | ((uint32_t)raw_entry[31] << 24);

    fat_meta_t *meta = (fat_meta_t *)node->priv;
    meta->first_cluster = cluster_lo;
    meta->file_size = (attr & FAT_ATTR_DIRECTORY) ? 0 : size;
    meta->dirent_lba = entry_lba;
    meta->dirent_offset = entry_offset;

    node->type = (attr & FAT_ATTR_DIRECTORY) ? VNODE_DIR : VNODE_FILE;
    node->ops = (attr & FAT_ATTR_DIRECTORY) ? &fat_dir_ops : &fat_file_ops;
    set_name(node, display_name);
    node->parent = ctx->parent;
    node->mode = (attr & FAT_ATTR_DIRECTORY) ? VFS_DEFAULT_DIR_MODE : VFS_DEFAULT_FILE_MODE;
    /* uid/gid stay 0 (root) -- this is foreign media originally populated
     * by host tooling (mkfs.fat/mtools); there's no ISUNUX process that
     * "created" any of these entries in the sense process_create() means,
     * even once ISUNUX itself starts writing to them */

    ctx->found = node;
    return 1;
}

static vnode_t *fat_dir_lookup(vnode_t *dir, const char *name) {
    fat_lookup_ctx_t ctx = { .want_name = name, .found = NULL, .parent = dir };
    fat_iterate_dir(dir, fat_lookup_visitor, &ctx);
    return ctx.found;
}

typedef struct {
    int want_index;
    char *name_out;
    uint64_t name_out_size;
    int found;
} fat_readdir_ctx_t;

static int fat_readdir_visitor(const char *display_name, const uint8_t *raw_entry, int real_index,
                                uint32_t entry_lba, uint32_t entry_offset, void *ctx_) {
    (void)raw_entry;
    (void)entry_lba;
    (void)entry_offset;
    fat_readdir_ctx_t *ctx = (fat_readdir_ctx_t *)ctx_;
    if (real_index != ctx->want_index) return 0;

    uint64_t i = 0;
    for (; display_name[i] && i + 1 < ctx->name_out_size; i++) ctx->name_out[i] = display_name[i];
    ctx->name_out[i] = '\0';
    ctx->found = 1;
    return 1;
}

static int fat_dir_readdir(vnode_t *dir, int index, char *name_out, uint64_t name_out_size) {
    fat_readdir_ctx_t ctx = { .want_index = index, .name_out = name_out, .name_out_size = name_out_size, .found = 0 };
    fat_iterate_dir(dir, fat_readdir_visitor, &ctx);
    return ctx.found ? 1 : 0;
}

static long fat_file_read(vnode_t *node, void *buf, uint64_t count, uint64_t offset) {
    fat_meta_t *meta = (fat_meta_t *)node->priv;
    if (offset >= meta->file_size) return 0; /* EOF */

    uint64_t avail = meta->file_size - offset;
    uint64_t n = count < avail ? count : avail;

    uint32_t cluster_size = (uint32_t)sectors_per_cluster * bytes_per_sector;
    uint32_t cluster = meta->first_cluster;

    uint64_t skip_clusters = offset / cluster_size;
    for (uint64_t i = 0; i < skip_clusters; i++) {
        cluster = fat_next_cluster(cluster);
        if (cluster < 2 || cluster >= FAT16_EOC_MIN) return 0; /* chain shorter than expected -- fail safe */
    }
    uint64_t pos_in_cluster = offset % cluster_size;

    uint8_t sector[512];
    uint8_t *dst = (uint8_t *)buf;
    uint64_t copied = 0;

    while (copied < n) {
        uint32_t sector_in_cluster = (uint32_t)(pos_in_cluster / bytes_per_sector);
        uint32_t pos_in_sector = (uint32_t)(pos_in_cluster % bytes_per_sector);
        uint32_t lba = cluster_to_lba(cluster) + sector_in_cluster;

        if (ata_read_sectors(lba, 1, sector) != 0) break; /* IO error -- return what we've copied so far */

        uint64_t space_in_sector = bytes_per_sector - pos_in_sector;
        uint64_t chunk = (n - copied) < space_in_sector ? (n - copied) : space_in_sector;
        for (uint64_t i = 0; i < chunk; i++) dst[copied + i] = sector[pos_in_sector + i];
        copied += chunk;
        pos_in_cluster += chunk;

        if (pos_in_cluster >= cluster_size) {
            pos_in_cluster = 0;
            cluster = fat_next_cluster(cluster);
            if (cluster < 2 || cluster >= FAT16_EOC_MIN) break; /* end of chain */
        }
    }
    return (long)copied;
}

static int fat_file_stat(vnode_t *node, uint64_t *size_out) {
    fat_meta_t *meta = (fat_meta_t *)node->priv;
    *size_out = meta->file_size;
    return 0;
}

/* ---- write support: extending/overwriting files that already exist
 * on disk. Deliberately does NOT include create()/mkdir()/unlink() --
 * those need free-directory-entry-slot scanning, which is different
 * enough (and risky enough to get subtly wrong) that it's a separate
 * step once this one's proven solid. See things.md's Tier 2 notes. ---- */

/* Writes `value` into cluster `cluster`'s FAT table entry, replicated
 * across every FAT copy (mkfs.fat defaults to 2) -- unlike reading
 * (fat_next_cluster, which only ever needs ONE consistent copy),
 * writing has to keep every copy in sync or a real fsck/other OS
 * reading this disk later would see mismatched FATs. Read-modify-write
 * per copy, since a sector holds 256 entries and this only ever
 * changes one of them. */
static void fat_write_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 2;
    uint32_t sector_in_fat = fat_offset / bytes_per_sector;
    uint32_t entry_off = fat_offset % bytes_per_sector;
    uint8_t sector[512];

    for (uint8_t copy = 0; copy < num_fats; copy++) {
        uint32_t lba = first_fat_sector + (uint32_t)copy * fat_size_16 + sector_in_fat;
        if (ata_read_sectors(lba, 1, sector) != 0) continue; /* best-effort per copy -- one
                                                                * bad copy shouldn't stop the
                                                                * others from staying correct */
        sector[entry_off] = (uint8_t)(value & 0xFF);
        sector[entry_off + 1] = (uint8_t)((value >> 8) & 0xFF);
        ata_write_sectors(lba, 1, sector);
    }
}

/* Finds the first free (0x0000) cluster in the FAT, marks it
 * end-of-chain immediately to reserve it, and returns its cluster
 * number. Returns 0 if the disk is full (or a read failed partway
 * through the scan) -- 0 is never a legal cluster number, same
 * sentinel meaning used everywhere else in this file. A linear scan
 * of the whole FAT on every single allocation is the "simplest thing
 * that works" choice, same philosophy as fat_next_cluster() re-reading
 * on every call rather than caching -- fine for a disk this small,
 * worth revisiting if a much bigger one ever makes it measurably slow. */
static uint32_t fat_alloc_cluster(void) {
    uint32_t entries_per_sector = bytes_per_sector / 2;
    uint32_t total_entries = (uint32_t)fat_size_16 * entries_per_sector;
    uint8_t sector[512];
    uint32_t loaded_sector_num = 0xFFFFFFFF; /* sentinel: nothing loaded yet */

    for (uint32_t idx = 2; idx < total_entries; idx++) { /* cluster numbers start at 2 -- 0 and 1 are reserved */
        uint32_t sector_num = idx / entries_per_sector;
        uint32_t entry_off = (idx % entries_per_sector) * 2;

        if (sector_num != loaded_sector_num) {
            if (ata_read_sectors(first_fat_sector + sector_num, 1, sector) != 0) return 0;
            loaded_sector_num = sector_num;
        }

        uint16_t val = (uint16_t)sector[entry_off] | ((uint16_t)sector[entry_off + 1] << 8);
        if (val == 0x0000) {
            fat_write_fat_entry(idx, FAT16_EOC_MIN);
            return idx;
        }
    }
    return 0; /* disk full */
}

/* Patches this node's own 32-byte directory entry on disk with its
 * current first_cluster/file_size -- called after every successful
 * write, so growth (or a freshly-allocated first cluster) actually
 * survives a reboot instead of just being correct for the currently
 * open fd. Best-effort: if the read or write fails, the in-memory
 * vnode is still correct for the rest of this boot, it just won't
 * persist -- there's nothing safer left to do at this layer if the
 * disk itself is failing. */
static void fat_writeback_dirent(vnode_t *node) {
    fat_meta_t *meta = (fat_meta_t *)node->priv;
    uint8_t sector[512];
    if (ata_read_sectors(meta->dirent_lba, 1, sector) != 0) return;

    uint8_t *entry = sector + meta->dirent_offset;
    entry[26] = (uint8_t)(meta->first_cluster & 0xFF);
    entry[27] = (uint8_t)((meta->first_cluster >> 8) & 0xFF);
    entry[28] = (uint8_t)(meta->file_size & 0xFF);
    entry[29] = (uint8_t)((meta->file_size >> 8) & 0xFF);
    entry[30] = (uint8_t)((meta->file_size >> 16) & 0xFF);
    entry[31] = (uint8_t)((meta->file_size >> 24) & 0xFF);

    ata_write_sectors(meta->dirent_lba, 1, sector);
}

static long fat_file_write(vnode_t *node, const void *buf, uint64_t count, uint64_t offset) {
    fat_meta_t *meta = (fat_meta_t *)node->priv;
    if (count == 0) return 0;

    uint32_t cluster_size = (uint32_t)sectors_per_cluster * bytes_per_sector;

    /* A genuinely empty file (never had a byte written to it) has
     * first_cluster == 0 -- give it its very first cluster before
     * anything else can happen. See fat_meta_t's doc comment for why
     * this can't collide with the root-directory sentinel meaning:
     * write is never called on a directory at all. */
    if (meta->first_cluster == 0) {
        uint32_t c = fat_alloc_cluster();
        if (c == 0) return -1; /* disk completely full, can't even start */
        meta->first_cluster = c;
    }

    /* Walk to the cluster containing `offset`, extending the chain
     * with freshly allocated clusters if the file doesn't reach that
     * far yet (writing past current EOF, e.g. an unbuffered append). */
    uint32_t cluster = meta->first_cluster;
    uint64_t cluster_index = offset / cluster_size;
    for (uint64_t i = 0; i < cluster_index; i++) {
        uint32_t next = fat_next_cluster(cluster);
        if (next < 2 || next >= FAT16_EOC_MIN) {
            uint32_t new_cluster = fat_alloc_cluster();
            if (new_cluster == 0) return 0; /* out of space before we even reached the write position */
            fat_write_fat_entry(cluster, new_cluster);
            next = new_cluster;
        }
        cluster = next;
    }

    uint64_t pos_in_cluster = offset % cluster_size;
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t sector[512];
    uint64_t copied = 0;

    while (copied < count) {
        uint32_t sector_in_cluster = (uint32_t)(pos_in_cluster / bytes_per_sector);
        uint32_t pos_in_sector = (uint32_t)(pos_in_cluster % bytes_per_sector);
        uint32_t lba = cluster_to_lba(cluster) + sector_in_cluster;

        uint64_t space_in_sector = bytes_per_sector - pos_in_sector;
        uint64_t chunk = (count - copied) < space_in_sector ? (count - copied) : space_in_sector;

        if (chunk < bytes_per_sector) {
            /* partial-sector write -- read-modify-write so the rest of
             * the sector's existing content isn't clobbered */
            if (ata_read_sectors(lba, 1, sector) != 0) break;
        }
        for (uint64_t i = 0; i < chunk; i++) sector[pos_in_sector + i] = src[copied + i];
        if (ata_write_sectors(lba, 1, sector) != 0) break;

        copied += chunk;
        pos_in_cluster += chunk;

        if (pos_in_cluster >= cluster_size && copied < count) {
            pos_in_cluster = 0;
            uint32_t next = fat_next_cluster(cluster);
            if (next < 2 || next >= FAT16_EOC_MIN) {
                uint32_t new_cluster = fat_alloc_cluster();
                if (new_cluster == 0) break; /* out of space -- return what we've written so far */
                fat_write_fat_entry(cluster, new_cluster);
                next = new_cluster;
            }
            cluster = next;
        }
    }

    if (offset + copied > meta->file_size) meta->file_size = offset + copied;
    fat_writeback_dirent(node); /* persist size + (possibly new) first_cluster -- see its own doc comment */

    return (long)copied;
}

static vnode_ops_t fat_file_ops = {
    .read = fat_file_read,
    .write = fat_file_write, /* extends/overwrites existing files -- no create()/mkdir()/unlink()
                               * yet, see the comment above fat_write_fat_entry for why those are
                               * a deliberately separate next step */
    .lookup = NULL,
    .readdir = NULL,
    .mkdir = NULL,
    .create = NULL,
    .unlink = NULL,
    .stat = fat_file_stat,
};

static vnode_ops_t fat_dir_ops = {
    .read = NULL,
    .write = NULL,
    .lookup = fat_dir_lookup,
    .readdir = fat_dir_readdir,
    .mkdir = NULL,
    .create = NULL,
    .unlink = NULL,
    .stat = NULL, /* dirs report size 0 via the generic fallback, same as tmpfs/devfs dirs */
};

/* ---- mount / install ---- */

int fat_mount(void) {
    fat_mounted = 0;

    if (!ata_present()) {
        serial_print("[fat] no disk present, skipping FAT mount\n");
        return -1;
    }

    uint8_t sector[512];
    if (ata_read_sectors(0, 1, sector) != 0) {
        serial_print("[fat] failed to read boot sector\n");
        return -1;
    }

    const fat16_bpb_t *bpb = (const fat16_bpb_t *)sector;

    if (bpb->signature != 0xAA55) {
        serial_print("[fat] no valid boot sector signature at LBA 0 (disk not formatted?)\n");
        return -1;
    }
    if (bpb->bytes_per_sector != 512) {
        serial_print("[fat] unsupported sector size (only 512-byte sectors supported)\n");
        return -1;
    }
    if (bpb->fat_size_16 == 0) {
        serial_print("[fat] fat_size_16 is 0 -- this looks like FAT32, not FAT16, skipping\n");
        return -1;
    }

    bytes_per_sector = bpb->bytes_per_sector;
    sectors_per_cluster = bpb->sectors_per_cluster;
    root_entry_count = bpb->root_entry_count;
    num_fats = bpb->num_fats;
    fat_size_16 = bpb->fat_size_16;

    first_fat_sector = bpb->reserved_sector_count;
    root_dir_sectors = ((uint32_t)root_entry_count * 32 + (bytes_per_sector - 1)) / bytes_per_sector;
    first_root_dir_sector = first_fat_sector + (uint32_t)num_fats * fat_size_16;
    first_data_sector = first_root_dir_sector + root_dir_sectors;

    node_count = 0;
    fat_mounted = 1;

    serial_print("[fat] FAT16 volume found, ");
    serial_print_dec(root_dir_sectors);
    serial_print(" root-dir sectors, data starts at LBA ");
    serial_print_dec(first_data_sector);
    serial_print("\n");
    return 0;
}

void fat_install(vnode_t *mnt_dir_vnode) {
    if (!fat_mounted) return;

    root_meta.first_cluster = 0; /* sentinel -- use the fixed root-dir sector range */
    root_meta.file_size = 0;

    mnt_dir_vnode->priv = &root_meta;
    mnt_dir_vnode->ops = &fat_dir_ops;
    /* deliberately not touching ->name or ->parent -- see fat.h's doc comment on this function */
}
