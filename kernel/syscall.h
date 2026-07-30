#pragma once
#include "idt.h"

#define SYS_WRITE 0
#define SYS_EXIT  1
#define SYS_OPEN  2
#define SYS_READ  3
#define SYS_CLOSE 4
#define SYS_BRK   5
#define SYS_FORK  6
#define SYS_EXECVE  7
#define SYS_WAITPID 8
#define SYS_READDIR 9
#define SYS_CHDIR   10
#define SYS_GETCWD  11
#define SYS_MKDIR   12
#define SYS_CREATE  13
#define SYS_UNLINK  14
#define SYS_STAT    15
#define SYS_DUP2    16
#define SYS_PIPE    17

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
