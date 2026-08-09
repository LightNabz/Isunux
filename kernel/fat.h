#pragma once
#include <stdint.h>
#include "vfs.h"

/* FAT16, whole-disk (no MBR/partition table -- the FAT boot sector is
 * assumed to sit at LBA 0 directly), exactly what `mkfs.fat -F 16
 * disk.img` produces when pointed at a raw image instead of a
 * partition. Only short (8.3) names are understood -- long-filename
 * (VFAT LFN) entries are recognized and silently skipped during
 * directory scans, and new entries are always written in 8.3 form
 * (truncated to fit, never generating a proper LFN) -- so a name
 * longer than 8.3 allows will round-trip lossily.
 *
 * Full read/write: existing files can be read and written (extend,
 * overwrite, append), and new files/directories can be created and
 * removed (rmdir refuses a non-empty directory, matching
 * tmpfs_dir_unlink's own rule). All of it persists to the actual
 * on-disk structures -- directory entries, FAT chains, both FAT
 * copies -- not just the in-memory vnode, so it survives a reboot.
 *
 * Display names are lowercased on the way out of every directory
 * scan (FAT short names are conventionally stored upper-case) --
 * this isn't just cosmetic: fat_dir_lookup() compares the query name
 * against that same lowercased form, so "cat /mnt/hello.txt" finds
 * "HELLO.TXT" on disk without needing separate case-insensitive
 * comparison logic. */

/* Reads and validates the boot sector/BPB from LBA 0 of the ATA
 * drive, and caches the derived geometry (FAT location, root
 * directory location, data area start) for every later read/lookup.
 * Returns 0 if a usable FAT16 volume was found, -1 otherwise (no
 * drive, bad signature, wrong sector size, or this looks like FAT32
 * instead -- see fat.c's fat_mount() for exactly what's checked).
 * Does NOT touch the vfs tree at all -- call fat_install() separately
 * once this succeeds. Safe to call once, any time after ata_init(). */
int fat_mount(void);

/* Wires the already-mounted FAT filesystem onto an existing tmpfs
 * directory -- same "ops-swap on a plain tmpfs dir" trick
 * devfs_install() uses for /dev, extended with vnode_t::priv since
 * FAT (unlike devfs's fixed device table) needs real per-node state
 * (which cluster, what size). Deliberately does NOT touch
 * mnt_dir_vnode's ->name or ->parent -- leaving those exactly as
 * tmpfs_create_dir() set them is what makes ".." and
 * vfs_canonical_path() keep working correctly right across the mount
 * boundary, with no special-casing anywhere else in the vfs layer.
 * A no-op if fat_mount() hasn't succeeded. */
void fat_install(vnode_t *mnt_dir_vnode);
