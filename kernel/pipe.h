#pragma once
#include "vfs.h"

/* Anonymous pipes: a fixed pool of fixed-size ring buffers, each with
 * a read-end and a write-end vnode. Reads/writes block by cooperatively
 * yield()ing (same pattern keyboard_read already uses) until there's
 * data/space, or until the other end's reference count hits zero (EOF
 * on read, "broken pipe" on write). See the dup/close vnode_ops on
 * vnode_ops_t for how that reference count is tracked correctly across
 * fork(), dup2(), and process exit. */

/* Allocates a pipe and fills read_end_out/write_end_out with its two
 * vnodes (to be installed into fd table slots by the caller -- this
 * module knows nothing about fd numbers). Returns 0 on success, -1 if
 * the pipe pool is exhausted. */
int pipe_create(vnode_t **read_end_out, vnode_t **write_end_out);
