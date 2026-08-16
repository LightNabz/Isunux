#pragma once
#include "idt.h"

/* Real Linux x86_64 syscall numbers -- 1f of the syscall-compat
 * sequence (see things.md). This is necessary but deliberately NOT
 * sufficient on its own: 1a-1e (entry mechanism, arg widening, auxv,
 * FS.base, negative errno) are what actually determine whether a
 * borrowed binary runs at all. Renumbering alone would just make a
 * broken call land on the right case label.
 *
 * Two things fell out of getting this right that are bigger than a
 * rename, both still part of this same milestone:
 *   - open(2)'s real signature is (path, flags, mode), not our old
 *     bare (path). SYS_CREATE is gone entirely -- real programs almost
 *     never call legacy creat(2) (85), they call open() with O_CREAT,
 *     same as this kernel's own SYS_OPEN case does now (see fcntl.h
 *     for the real O_* flag values, process_open() in process.c for
 *     the implementation).
 *   - mmap(2)'s real signature is 6 args (adds fd, offset), which our
 *     dispatch now reads and validates instead of silently ignoring.
 *
 * set_tid_address/set_robust_list are safe no-op-ish stubs -- musl's
 * _start calls both unconditionally even for a single-threaded
 * program, and neither one means anything without a real threading
 * model underneath (there isn't one -- every task on this kernel IS a
 * whole process). exit_group is wired to the exact same handler as
 * exit -- distinct in real Linux (one thread vs the whole process),
 * identical here since there's no "one thread of a process" concept
 * to distinguish them. */
#define SYS_READ           0
#define SYS_WRITE          1
#define SYS_OPEN           2
#define SYS_CLOSE          3
#define SYS_STAT           4
#define SYS_MMAP           9
#define SYS_MUNMAP         11
#define SYS_BRK            12
#define SYS_PIPE           22
#define SYS_DUP2           33
#define SYS_GETPID         39
#define SYS_FORK           57
#define SYS_EXECVE         59
#define SYS_EXIT           60
#define SYS_WAITPID        61 /* real name: wait4 */
#define SYS_KILL           62
#define SYS_GETCWD         79
#define SYS_CHDIR          80
#define SYS_MKDIR          83
#define SYS_UNLINK         87
#define SYS_CHMOD          90
#define SYS_CHOWN          92
#define SYS_GETUID         102
#define SYS_GETGID         104
#define SYS_SETUID         105
#define SYS_SETGID         106
#define SYS_ARCH_PRCTL     158
#define SYS_READDIR        217 /* real name: getdents64 */
#define SYS_SET_TID_ADDRESS 218
#define SYS_EXIT_GROUP     231
#define SYS_SET_ROBUST_LIST 273

/* ISUNUX-native extensions with no real Linux equivalent at all -- real
 * job control and raw-mode terminal handling both go through ioctl(2)
 * (TIOCSPGRP, TCSETS/termios), which this kernel doesn't implement.
 * Parked at a deliberately out-of-band range so these can never collide
 * with a real syscall number now or after some future kernel version
 * adds more of them -- a borrowed binary simply never knows these
 * numbers exist, only ISUNUX's own recompiled mini_libc programs call
 * them. A borrowed shell wanting real job control is a known, separate
 * gap this doesn't attempt to close (Toybox's basic utils don't need
 * it at all, so it isn't a 1g blocker). */
#define SYS_SET_FOREGROUND 1000
#define SYS_TTY_SET_RAW    1001
#define SYS_TTY_SET_ECHO   1002

/* Real Linux's arch_prctl "code" values -- an argument value, not a
 * syscall number, so there was never a reason to wait on these. */
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

/* Real Linux prot/flags values, same "why invent our own" reasoning as
 * the signal numbers above. Only what our anonymous-only mmap()
 * actually uses is defined -- there's no file-backed mapping yet (needs
 * Tier 2's real filesystem), so MAP_SHARED doesn't mean anything
 * without a file or without real fork()-time mapping-list bookkeeping
 * to share against, so it's not defined here either -- every mapping
 * this kernel creates behaves like MAP_PRIVATE regardless of what's
 * passed. */
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

/* Called from syscall_entry (isr.asm) with a pointer to the saved
 * register frame. Syscall number comes in via rax, arguments via the
 * full 6-register x86_64 `syscall` ABI: rdi/rsi/rdx/r10/r8/r9.
 * The return value is written straight into frame->rax, which is the
 * same memory syscall_entry's epilogue pops back into the real rax
 * register on the way out -- so writing frame->rax IS setting the
 * syscall's return value. */
void syscall_handler(interrupt_frame_t *frame);
