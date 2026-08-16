#pragma once

/* Real Linux x86_64 errno values -- not our own numbering. A syscall's
 * return convention is exactly this: 0 or positive on success, one of
 * these NEGATED on failure (e.g. a missing file is -ENOENT, not -1).
 * Real libc's own syscall() wrapper (musl, glibc -- whatever a borrowed
 * static binary was built with) checks for a return value in
 * (-4096, -1], negates it back, and stores it in that program's own
 * errno global -- which is why matching the real numbers here matters,
 * not just "some negative value on failure". This is deliberately just
 * the numbers and the calling convention, not a whole errno subsystem
 * (no per-thread errno storage on ISUNUX's own side, no strerror() --
 * that's Tier 4 territory, not needed for what THIS milestone is
 * actually about: what a syscall hands back over the trap boundary). */

#define EPERM   1  /* operation permitted only to the owner or root */
#define ENOENT  2  /* no such file or directory */
#define ESRCH   3  /* no such process -- kill()/etc. targeting a pid that doesn't exist */
#define ENODEV  19 /* file-backed mmap() -- not supported, this kernel only has anonymous mappings */
#define ENOEXEC 8  /* not a valid/loadable executable -- empty file, malformed ELF, etc. */
#define EIO     5  /* real I/O error -- ata.c/fat.c internal failures surface as this, since nothing above them can currently tell WHY the disk failed, only that it did */
#define EAGAIN  11 /* a fixed-size kernel resource pool (processes, tasks) is exhausted right now -- real fork()'s own convention for this, distinct from ENOMEM's "allocation itself failed" */
#define EBADF   9  /* fd isn't open, or isn't open for the operation attempted */
#define ECHILD  10 /* waitpid() with no matching child at all */
#define ENOMEM  12 /* allocation failure */
#define EACCES  13 /* file permission bits deny this */
#define EEXIST  17 /* create()/mkdir() target already exists */
#define ENOTDIR 20 /* expected a directory, found something else */
#define EISDIR  21 /* expected a file, found a directory */
#define EINVAL  22 /* argument doesn't make sense (bad signal number, bad fd number range, etc.) */
#define ENFILE  23 /* system-wide resource (e.g. the pipe table) is full */
#define EMFILE  24 /* this process's own fd table is full */
#define ENOSPC  28 /* disk is full */
#define EROFS   30 /* filesystem/directory doesn't support this operation at all (e.g. mkdir on devfs) -- not a permission-bits question, there's no ops hook for it in the first place */
#define ENOTEMPTY 39 /* rmdir()/unlink() on a non-empty directory */
#define ENOSYS  38 /* syscall number itself isn't implemented at all -- distinct from EINVAL, which is for a bad ARGUMENT to a syscall that does exist */
