#include "gdt.h"
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

/* 3 entries: null, kernel code (selector 0x08), kernel data (selector 0x10) */
static uint64_t gdt[3];
static gdt_ptr_t gdt_ptr;

/* Descriptor bit positions, long-mode flat segments (base/limit unused by
 * the CPU in 64-bit mode for code/data, so we only set the flags that
 * matter: present, "not a system segment", executable, writable, and the
 * long-mode bit for the code segment). */
#define GDT_PRESENT   (1ULL << 47)
#define GDT_NOTSYS    (1ULL << 44) /* S bit: 1 = code/data, 0 = system (TSS etc) */
#define GDT_EXEC      (1ULL << 43)
#define GDT_RW        (1ULL << 41)
#define GDT_LONGMODE  (1ULL << 53)

void gdt_init(void) {
    gdt[0] = 0;
    gdt[1] = GDT_PRESENT | GDT_NOTSYS | GDT_EXEC | GDT_RW | GDT_LONGMODE; /* 0x08: kernel code */
    gdt[2] = GDT_PRESENT | GDT_NOTSYS | GDT_RW;                          /* 0x10: kernel data */

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    /* lgdt loads the table, but CS can't be reloaded with a plain mov —
     * the classic trick is a far return: push the new CS selector and a
     * target address, then lretq "returns" into that selector. */
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
        :
        : "m"(gdt_ptr)
        : "rax", "memory"
    );
}
