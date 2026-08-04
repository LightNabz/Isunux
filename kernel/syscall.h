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
#define SYS_KILL    18
#define SYS_GETPID  19
#define SYS_MMAP    20
#define SYS_MUNMAP  21
#define SYS_SET_FOREGROUND 22

/* Real Linux prot/flags values, same "why invent our own" reasoning as
 * the signal numbers above. Only what our anonymous-only mmap()
 * actually uses is defined -- there's no file-backed mapping yet (needs
 * Tier 2's real filesystem), so fd/offset aren't part of our syscall's
 * ABI at all, and MAP_SHARED doesn't mean anything without a file or
 * without real fork()-time mapping-list bookkeeping to share against,
 * so it's not defined here either -- every mapping this kernel creates
 * behaves like MAP_PRIVATE regardless of what's passed. */
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#define MAP_ANONYMOUS 0x20

/* Real Linux signal numbers, kept unchanged on purpose -- no reason to
 * invent our own numbering when Tier 3 wants real Linux ABI
 * compatibility eventually anyway, and it's free to just use the real
 * ones now while the signal set is still this small. Only these four
 * exist at all right now ("the four horsemen"), and all four have a
 * hardcoded default action -- there's no sigaction()/handler mechanism
 * yet, so a process can't catch, block, or ignore any of them (SIGCHLD
 * aside, whose default action really is "ignore"). */
#define SIGINT  2
#define SIGSEGV 11
#define SIGKILL 9
#define SIGTERM 15
#define SIGCHLD 17
#define SIGTSTP 20
#define SIGCONT 18

/* Real POSIX wait-status bit layout (glibc's bits/waitstatus.h), used
 * exactly as-is -- no reason to invent our own encoding when this is
 * exactly the kind of thing Tier 3's Linux ABI compat goal needs
 * unchanged anyway, same reasoning as the signal numbers above.
 *   exited normally, code C:  (C & 0xff) << 8            (low byte 0)
 *   killed by signal S:        S & 0x7f                    (high byte 0)
 *   stopped by signal S:      ((S & 0xff) << 8) | 0x7f
 * These three encodings can never collide: an exited status always has
 * a zero low byte, a signaled status always has a nonzero low byte that
 * isn't 0x7f, and a stopped status's low byte is always exactly 0x7f.
 * The kernel only ever WRITES these (process_terminate's callers below,
 * and process_waitpid's stop-report path); userland's matching
 * WIFEXITED/WEXITSTATUS/etc. decode macros live in mini_libc.h, the
 * only place that ever reads them back apart. */
#define ENCODE_EXITED(code)    (((code) & 0xff) << 8)
#define ENCODE_SIGNALED(sig)   ((sig) & 0x7f)
#define ENCODE_STOPPED(sig)    ((((sig) & 0xff) << 8) | 0x7f)

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
