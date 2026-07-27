#pragma once
#include <stdint.h>

/* Long-mode TSS. We only actually care about rsp0 -- the kernel stack
 * the CPU auto-loads the instant a ring-3 program interrupts/traps into
 * ring 0. No hardware task-switching, no IST stacks (yet), no I/O
 * bitmap (iomap_base is set past the segment limit on purpose, which
 * means "no bitmap, all ports off-limits from ring 3"). */
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss_t;

extern tss_t kernel_tss;

void tss_init(void);

/* Called every time we're about to run a different task, so a ring3->0
 * trap always lands on the RIGHT task's kernel stack, not some other
 * task's leftover one. */
void tss_set_kernel_stack(uint64_t rsp0);
