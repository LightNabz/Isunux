#pragma once
#include "idt.h"

/* Implements fork(): clones the calling process's address space and fd
 * table, creates a new schedulable task whose kernel stack is hand-built
 * to resume through the exact same syscall-return path a real task
 * would use, and splices it into the round-robin ring. Returns the
 * child's pid (the caller writes that into the parent's frame->rax), or
 * -1 on failure. The child's own "return value" (0) is baked directly
 * into the copied frame this function builds -- nobody needs to
 * special-case "oh, this is the child" anywhere else. */
int64_t do_fork(interrupt_frame_t *parent_frame);
