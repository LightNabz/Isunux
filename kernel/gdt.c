#include "gdt.h"
#include "tss.h"
#include "kutil.h"
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

/* 8 qword-sized slots: null, kernel code, kernel data, an unused user
 * code slot (placeholder -- see gdt.h), user data, user code, then the
 * 16-byte TSS descriptor spans the last two slots. */
static uint64_t gdt[8];
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

    tss_descriptor_t *slot = (tss_descriptor_t *)&gdt[6];
    *slot = desc;
}

void gdt_init(void) {
    gdt[0] = 0;
    gdt[1] = GDT_PRESENT | GDT_NOTSYS | GDT_EXEC | GDT_RW | GDT_LONGMODE;            /* 0x08 kernel code */
    gdt[2] = GDT_PRESENT | GDT_NOTSYS | GDT_RW;                                     /* 0x10 kernel data */
    /* 0x18 -- placeholder only. SYSRET derives SS from (STAR[63:48]+8)
     * and CS from (STAR[63:48]+16), so it needs a slot to sit AT
     * STAR[63:48] even though nothing ever loads a segment register
     * from it. Not marked GDT_LONGMODE since it's standing in for a
     * legacy 32-bit code segment -- doesn't matter in practice since
     * it's never executed, but matches what it's pretending to be. */
    gdt[3] = GDT_PRESENT | GDT_NOTSYS | GDT_EXEC | GDT_RW | GDT_DPL3;                /* 0x18 user32 code (unused) */
    gdt[4] = GDT_PRESENT | GDT_NOTSYS | GDT_RW | GDT_DPL3;                          /* 0x20 user data   */
    gdt[5] = GDT_PRESENT | GDT_NOTSYS | GDT_EXEC | GDT_RW | GDT_LONGMODE | GDT_DPL3; /* 0x28 user code   */

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
        "mov $0x30, %%ax\n"
        "ltr %%ax\n"
        :
        : "m"(gdt_ptr)
        : "rax", "memory"
    );
}

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_FMASK  0xC0000084
#define EFER_SCE   (1ULL << 0) /* SYSCALL/SYSRET enable */

/* isr.asm's SYSCALL entry point -- IA32_LSTAR points straight at it,
 * completely bypassing the IDT (this is not an interrupt gate, there's
 * no vector, no idt_set_gate() call for this anywhere). */
extern void syscall_entry(void);

/* Must run after gdt_init() -- STAR encodes the exact selector values
 * gdt_init() just built, and gets it wrong silently (no fault at setup
 * time) if the GDT isn't in its final shape yet. */
void syscall_init(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SCE); /* Limine leaves us in long mode already, but SCE is a separate bit it doesn't set for us */

    /* STAR[47:32]: SYSCALL loads this into CS, and this+8 into SS --
     * kernel code/data are adjacent for exactly this reason.
     * STAR[63:48]: SYSRET's base selector -- see GDT_USER32_CODE's
     * comment in gdt.c for why 0x18 (not the user data/code selectors
     * themselves) is the right value here. */
    uint64_t star = ((uint64_t)GDT_USER32_CODE << 48) | ((uint64_t)GDT_KERNEL_CODE << 32);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* Bits set here are CLEARED in RFLAGS the instant SYSCALL fires,
     * before syscall_entry's first instruction runs. Clearing IF (bit
     * 9) is what keeps an IRQ from landing mid-transition, before the
     * stack switch in syscall_entry is complete -- exactly what a
     * 0x8E interrupt gate already did automatically for `int 0x80`,
     * so this isn't a new behavior, just the SYSCALL-path equivalent.
     * TF (bit 8) cleared too, so a stray user-mode single-step flag
     * can't trip anything on the way in. */
    wrmsr(MSR_FMASK, 0x300);
}
