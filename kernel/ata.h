#pragma once
#include <stdint.h>

/* Legacy ATA PIO driver -- primary bus, master drive only.
 *
 * Scope, deliberately: this drives the PRIMARY channel's MASTER drive
 * (the classic 0x1F0-0x1F7 / 0x3F6 legacy ISA ports) in polled PIO
 * mode, LBA28 addressing. No IRQ14, no secondary channel, no slave
 * drive, no DMA, no AHCI, no PCI enumeration at all -- piix3-ide (or
 * any ISA-compatibility-mode IDE controller) answers on these exact
 * fixed ports without needing PCI config space to find it, which is
 * the whole reason this is the simplest real block device to bring up
 * first. See things.md's Tier 2 notes for why AHCI/virtio-blk are
 * deliberately-deferred, not forgotten.
 *
 * Everything here is synchronous: a read/write call busy-polls the
 * status register until the drive is ready, then returns. That's a
 * real cost (the CPU can't do anything else meanwhile) but it's the
 * correct place to start -- interrupt-driven I/O is a valid later
 * upgrade once something (a real filesystem doing real work) actually
 * needs the CPU time back. */

/* Probes the primary bus for a master drive via IDENTIFY DEVICE and
 * records what it finds. Safe to call exactly once, early in boot,
 * after interrupts are enabled (polling doesn't need them, but the
 * PIT/IDT being up first keeps boot ordering simple to reason about).
 * Every other ata_* call is a no-op-and-fail if this was never called
 * or found nothing -- there's no separate "did you forget to init"
 * failure mode to worry about. */
void ata_init(void);

/* Whether ata_init() found a usable drive. Every ata_read_sectors /
 * ata_write_sectors call already checks this internally and fails
 * safely if not -- this is for callers (filesystem code, later) that
 * want to know up front whether persistence is even possible this
 * boot, rather than finding out on the first read. */
int ata_present(void);

/* Total addressable 512-byte sectors, from IDENTIFY's LBA28 total-
 * sector-count field. 0 if ata_present() is false. Callers should
 * bounds-check LBAs against this before calling read/write -- this
 * driver does NOT reject an out-of-range LBA itself (see
 * ata_read_sectors's doc comment for why). */
uint32_t ata_sector_count(void);

/* Reads `count` consecutive 512-byte sectors starting at `lba` into
 * `buf` (must have room for count*512 bytes). Returns 0 on success,
 * -1 on failure (no drive present, drive reported an error, or the
 * drive never became ready -- see ata.c's ATA_POLL_MAX_ITERS comment
 * for why this can't hang forever on bad hardware).
 *
 * Deliberately does NOT bounds-check `lba`+`count` against
 * ata_sector_count() -- the drive itself will report an error via the
 * status register's ERR bit for a genuinely out-of-range LBA, and
 * this driver correctly turns that into a -1 return either way. Doing
 * the check twice (here AND relying on hardware) would just be
 * redundant, not safer. */
int ata_read_sectors(uint32_t lba, uint8_t count, void *buf);

/* Same contract as ata_read_sectors, but writes. Issues a CACHE FLUSH
 * (0xE7) after the write completes and waits for the drive to
 * confirm it before returning -- without this, a QEMU/real-hardware
 * power loss right after a "successful" write could still lose data
 * sitting in the drive's write cache. This is exactly the kind of
 * thing that's invisible in every normal test and only ever shows up
 * as "my file was empty after a crash," so it's worth paying the
 * (tiny, on a virtual disk) extra wait for unconditionally rather
 * than only in some later "flush" call nothing remembers to make. */
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf);
