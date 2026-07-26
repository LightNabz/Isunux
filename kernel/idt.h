#pragma once
#include <stdint.h>

/* Must match the push order in isr.asm exactly: last pushed = lowest
 * address = first struct member. No RSP/SS here because we never change
 * privilege level yet (ring0 -> ring0 interrupts don't get those pushed
 * by the CPU). */
typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags;
} interrupt_frame_t;

void idt_init(void);
