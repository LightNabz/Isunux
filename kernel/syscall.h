#pragma once
#include "idt.h"

#define SYS_WRITE 0
#define SYS_EXIT  1

/* Called from isr128 (isr.asm) with a pointer to the saved register
 * frame. Syscall number comes in via rax, arguments via rdi/rsi/rdx
 * (our own convention -- loosely mirrors the real x86_64 `syscall` ABI
 * since that's a reasonable, well-trodden layout to imitate, even
 * though we're entering via `int 0x80` rather than `syscall` for now).
 * The return value is written straight into frame->rax, which is the
 * same memory the isr128 epilogue pops back into the real rax register
 * on the way out -- so writing frame->rax IS setting the syscall's
 * return value. */
void syscall_handler(interrupt_frame_t *frame);
