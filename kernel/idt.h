#pragma once
#include <stdint.h>

/* Must match the push order in isr.asm exactly: last pushed = lowest
 * address = first struct member. rsp/ss are only meaningful when this
 * frame came from a ring3->0 transition (e.g. the syscall gate) --
 * ring0->0 exceptions/IRQs never have them pushed by hardware, so
 * those two fields just sit on whatever memory happens to follow in
 * that case. Harmless as long as nothing reads them for a ring0-only
 * trap, and nothing in this codebase does. Declaring them here
 * unconditionally (rather than only for the syscall path) is what lets
 * fork() copy a syscall frame in one sizeof()-sized block without
 * silently dropping the two fields that actually matter for getting
 * back to ring 3 correctly. */
typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} interrupt_frame_t;

void idt_init(void);
