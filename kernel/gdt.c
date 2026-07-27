#include "gdt.h"
#include "tss.h"
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

/* A TSS descriptor is 16 bytes (twice the size of a normal code/data
 * descriptor) because it needs a full 64-bit base address. */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  limit_high_flags; /* low nibble = limit bits 16-19, high nibble = flags */
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} tss_descriptor_t;

/* 7 qword-sized slots: null, kernel code, kernel data, user data, user
 * code, then the 16-byte TSS descriptor spans the last two slots. */
static uint64_t gdt[7];
static gdt_ptr_t gdt_ptr;

/* Same bit positions as milestone 1/2, plus DPL now actually used:
 * P (present), S (not-a-system-segment), E (executable), RW
 * (readable-code / writable-data), L (64-bit code), and now DPL (ring
 * required to use this segment -- 0 for kernel segments, 3 for user
 * ones, encoded in bits 46:45). */
#define GDT_PRESENT   (1ULL << 47)
#define GDT_NOTSYS    (1ULL << 44)
#define GDT_EXEC      (1ULL << 43)
#define GDT_RW        (1ULL << 41)
#define GDT_LONGMODE  (1ULL << 53)
#define GDT_DPL3      (3ULL << 45)

static void set_tss_descriptor(uint64_t base, uint32_t limit) {
    tss_descriptor_t desc;
    desc.limit_low = (uint16_t)(limit & 0xFFFF);
    desc.base_low = (uint16_t)(base & 0xFFFF);
    desc.base_mid = (uint8_t)((base >> 16) & 0xFF);
    desc.access = 0x89; /* present, DPL=0, type=1001 (64-bit TSS, available) */
    desc.limit_high_flags = (uint8_t)((limit >> 16) & 0x0F);
    desc.base_high = (uint8_t)((base >> 24) & 0xFF);
    desc.base_upper = (uint32_t)(base >> 32);
    desc.reserved = 0;

    tss_descriptor_t *slot = (tss_descriptor_t *)&gdt[5];
    *slot = desc;
}

void gdt_init(void) {
    gdt[0] = 0;
    gdt[1] = GDT_PRESENT | GDT_NOTSYS | GDT_EXEC | GDT_RW | GDT_LONGMODE;            /* 0x08 kernel code */
    gdt[2] = GDT_PRESENT | GDT_NOTSYS | GDT_RW;                                     /* 0x10 kernel data */
    gdt[3] = GDT_PRESENT | GDT_NOTSYS | GDT_RW | GDT_DPL3;                          /* 0x18 user data   */
    gdt[4] = GDT_PRESENT | GDT_NOTSYS | GDT_EXEC | GDT_RW | GDT_LONGMODE | GDT_DPL3; /* 0x20 user code   */

    tss_init();
    set_tss_descriptor((uint64_t)&kernel_tss, sizeof(tss_t) - 1);

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    asm volatile (
        "lgdt %0\n"
        "push $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
        :
        : "m"(gdt_ptr)
        : "rax", "memory"
    );
}
