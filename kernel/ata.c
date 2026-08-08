#include "ata.h"
#include "kutil.h"
#include "serial.h"

/* Legacy primary IDE channel -- fixed ISA-compatibility-mode ports,
 * no PCI config space access needed (see ata.h's top comment). */
#define ATA_IO_BASE      0x1F0
#define ATA_CTRL_BASE    0x3F6

#define ATA_REG_DATA     (ATA_IO_BASE + 0) /* 16-bit */
#define ATA_REG_ERROR    (ATA_IO_BASE + 1) /* read */
#define ATA_REG_FEATURES (ATA_IO_BASE + 1) /* write */
#define ATA_REG_SECCOUNT (ATA_IO_BASE + 2)
#define ATA_REG_LBA_LOW  (ATA_IO_BASE + 3)
#define ATA_REG_LBA_MID  (ATA_IO_BASE + 4)
#define ATA_REG_LBA_HIGH (ATA_IO_BASE + 5)
#define ATA_REG_DRIVE    (ATA_IO_BASE + 6)
#define ATA_REG_STATUS   (ATA_IO_BASE + 7) /* read -- reading this ACKs a pending IRQ, which
                                             * is exactly why the 400ns settle-delay below
                                             * reads the ALT status register instead */
#define ATA_REG_COMMAND  (ATA_IO_BASE + 7) /* write */

#define ATA_REG_ALT_STATUS (ATA_CTRL_BASE + 0) /* read -- same bits as REG_STATUS, but reading
                                                 * it does NOT ack an IRQ and has no side effects,
                                                 * which is exactly what a "just let the hardware
                                                 * settle" delay wants */

#define ATA_STATUS_BSY  0x80 /* drive busy -- nothing else means anything while this is set */
#define ATA_STATUS_DRDY 0x40 /* drive ready to accept a command */
#define ATA_STATUS_DF   0x20 /* drive fault */
#define ATA_STATUS_DRQ  0x08 /* data request -- drive wants a PIO word transfer now */
#define ATA_STATUS_ERR  0x01 /* error -- read REG_ERROR for detail; we just fail the call */

#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH   0xE7

#define ATA_DRIVE_MASTER_LBA 0xE0 /* base drive-select byte: master, LBA mode (vs. old CHS) */

/* Real hardware settles in microseconds; a missing/broken drive
 * should never actually reach this cap. It exists purely so a bug (or
 * genuinely absent hardware someone forgot to attach) turns into a
 * clean -1 failure instead of the kernel hanging forever in a poll
 * loop -- same "never trust hardware to cooperate" discipline as the
 * poll caps already used for job-control waits elsewhere in this
 * codebase. Not a real timing measurement, just a big, safe ceiling. */
#define ATA_POLL_MAX_ITERS 100000

static int ata_found = 0;
static uint32_t ata_total_sectors = 0;

/* The spec's "wait ~400ns after selecting a drive before trusting the
 * status register" rule. Reading the ALT status register four times
 * back to back is the standard OSDev-wiki way to burn that much time
 * -- each read costs roughly the ISA bus's ~100ns access time on real
 * hardware, and unlike the real status register, reading ALT status
 * has no side effects (doesn't ack an IRQ), so it's safe to do purely
 * for the delay. */
static void ata_delay_400ns(void) {
    for (int i = 0; i < 4; i++) inb(ATA_REG_ALT_STATUS);
}

/* Polls until BSY clears. Returns 0 once it does, -1 if it never does
 * within ATA_POLL_MAX_ITERS (see that macro's comment). */
static int ata_wait_not_busy(void) {
    for (int i = 0; i < ATA_POLL_MAX_ITERS; i++) {
        if ((inb(ATA_REG_STATUS) & ATA_STATUS_BSY) == 0) return 0;
    }
    return -1;
}

/* Polls until the drive is ready to transfer a word (DRQ set), or
 * reports it can't (ERR or DF set). Returns 0 on DRQ, -1 on
 * error/fault/timeout -- callers don't need to distinguish those
 * cases, they all just mean "this sector isn't happening." */
static int ata_wait_drq(void) {
    for (int i = 0; i < ATA_POLL_MAX_ITERS; i++) {
        uint8_t status = inb(ATA_REG_STATUS);
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return -1;
        if (status & ATA_STATUS_DRQ) return 0;
    }
    return -1;
}

void ata_init(void) {
    ata_found = 0;
    ata_total_sectors = 0;

    /* Select master drive, LBA mode, LBA bits all 0 -- IDENTIFY
     * doesn't take an LBA, but the drive-select write still has to
     * happen (and settle) before poking any other register. */
    outb(ATA_REG_DRIVE, ATA_DRIVE_MASTER_LBA);
    ata_delay_400ns();

    /* If nothing answers on this bus at all, the status register
     * floats and reads back 0xFF -- that's the standard "no drive
     * here" signal, worth checking before we go poll BSY forever. */
    if (inb(ATA_REG_STATUS) == 0xFF) {
        serial_print("[ata] no drive found on primary bus\n");
        return;
    }

    outb(ATA_REG_SECCOUNT, 0);
    outb(ATA_REG_LBA_LOW, 0);
    outb(ATA_REG_LBA_MID, 0);
    outb(ATA_REG_LBA_HIGH, 0);
    outb(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    if (inb(ATA_REG_STATUS) == 0) {
        /* status reads back 0 right after the command -- drive genuinely doesn't exist */
        serial_print("[ata] no drive found on primary bus\n");
        return;
    }

    if (ata_wait_not_busy() != 0) {
        serial_print("[ata] drive present but never cleared BSY, giving up\n");
        return;
    }

    /* An ATAPI device (CD-ROM, not a disk) answers IDENTIFY
     * differently -- it sets LBA_MID/LBA_HIGH to a signature
     * (0x14/0xEB) instead of proceeding to DRQ. We only speak plain
     * ATA disks here, so treat that as "nothing usable found" rather
     * than misinterpreting its IDENTIFY response as disk geometry. */
    if (inb(ATA_REG_LBA_MID) != 0 || inb(ATA_REG_LBA_HIGH) != 0) {
        serial_print("[ata] device present but not a plain ATA disk (ATAPI?), skipping\n");
        return;
    }

    if (ata_wait_drq() != 0) {
        serial_print("[ata] drive present but IDENTIFY never signaled data ready\n");
        return;
    }

    uint16_t identify_data[256];
    insw(ATA_REG_DATA, identify_data, 256);

    /* Words 60-61 of the IDENTIFY response are the LBA28 total
     * sector count, low word first -- straight from the ATA spec's
     * fixed IDENTIFY layout. */
    ata_total_sectors = (uint32_t)identify_data[60] | ((uint32_t)identify_data[61] << 16);

    ata_found = 1;
    serial_print("[ata] primary master found, ");
    serial_print_dec(ata_total_sectors);
    serial_print(" sectors (");
    serial_print_dec((ata_total_sectors / 2048)); /* sectors -> MiB, 512B/sector */
    serial_print(" MiB)\n");
}

int ata_present(void) {
    return ata_found;
}

uint32_t ata_sector_count(void) {
    return ata_total_sectors;
}

/* Selects the master drive for an LBA28 command and pushes the
 * 28-bit address + sector count into the task-file registers. Shared
 * by read and write since everything up to the actual command byte
 * (0x20 vs 0x30) is identical. */
static void ata_setup_lba28(uint32_t lba, uint8_t count) {
    outb(ATA_REG_DRIVE, (uint8_t)(ATA_DRIVE_MASTER_LBA | ((lba >> 24) & 0x0F)));
    ata_delay_400ns();
    outb(ATA_REG_SECCOUNT, count);
    outb(ATA_REG_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (!ata_found || count == 0) return -1;

    ata_setup_lba28(lba, count);
    outb(ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

    uint8_t *dst = (uint8_t *)buf;
    for (uint8_t i = 0; i < count; i++) {
        /* the drive raises DRQ once PER SECTOR when count > 1, not
         * once for the whole transfer -- so this waits again before
         * each 512-byte chunk, not just once up front. */
        if (ata_wait_drq() != 0) return -1;
        insw(ATA_REG_DATA, dst, 256); /* 256 words = 512 bytes = one sector */
        dst += 512;
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if (!ata_found || count == 0) return -1;

    ata_setup_lba28(lba, count);
    outb(ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

    const uint8_t *src = (const uint8_t *)buf;
    for (uint8_t i = 0; i < count; i++) {
        if (ata_wait_drq() != 0) return -1;
        outsw(ATA_REG_DATA, src, 256);
        src += 512;
    }

    /* Flush the drive's write cache and wait for it to confirm --
     * see ata.h's doc comment on this function for why this isn't
     * optional. */
    if (ata_wait_not_busy() != 0) return -1;
    outb(ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    if (ata_wait_not_busy() != 0) return -1;

    return 0;
}
