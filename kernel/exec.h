#pragma once
#include "idt.h"

/* Implements execve(): loads a fresh ELF from the VFS, builds a
 * brand-new address space + stack for it, and overwrites the calling
 * task's *current* trap frame (rip/rsp/registers) so that when
 * isr128's normal epilogue runs, it iretq's straight into the new
 * program instead of back into the old one. No new resume mechanism
 * needed -- same trap, same task, just a different destination.
 *
 * Only returns (with -1) on failure, exactly like real execve(). On
 * success it doesn't "return" in any meaningful sense -- frame->rip no
 * longer points anywhere in the caller's old code. */
int64_t do_exec(interrupt_frame_t *frame, const char *path, char **user_argv);
