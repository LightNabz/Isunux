#pragma once
#include <stdint.h>
#include "vfs.h"

/* FAT16, whole-disk (no MBR/partition table -- the FAT boot sector is
 * assumed to sit at LBA 0 directly), exactly what `mkfs.fat -F 16
 * disk.img` produces when pointed at a raw image instead of a
 * partition. Only short (8.3) names are understood -- long-filename
 * (VFAT LFN) entries are recognized and silently skipped during
 * directory scans, never partially misread as a short name.
 *
 * Reading and writing EXISTING files both work (extend, overwrite,
 * append -- growth persists to the on-disk directory entry, not just
 * the in-memory vnode). Creating new files/directories and removing
 * entries do NOT yet -- those need free-directory-entry-slot
 * scanning, which is different enough (and risky enough to get
 * subtly wrong) that it's a deliberately separate next step once
 * write-to-existing-files is proven solid. So for now: whatever files
 * you want to read or modify need to already exist on the volume,
 * put there by host tooling (mkfs.fat + mtools) before boot. See
 * things.md's Tier 2 notes.
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
