#pragma once

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

/* Mirrors vfs_stat_t in kernel/vfs.h exactly -- this is the
 * kernel/userland ABI, kept in sync by hand like the SYS_* numbers
 * above. type is VNODE_FILE(0) or VNODE_DIR(1). */
typedef struct {
    unsigned long type;
    unsigned long size;
} stat_t;
#define VNODE_FILE_T 0
#define VNODE_DIR_T  1

/* Same convention as the kernel's syscall_handler expects: syscall
 * number in rax, args in rdi/rsi/rdx, triggered via `int 0x80`. */
static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    asm volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

/* Same as syscall3, plus a 4th arg passed in r10 -- matching the real
 * x86_64 syscall ABI's convention for a 4th argument (glibc's own
 * syscall() wrapper does exactly this same "register asm" trick to
 * pin a value to r10 specifically). */
static inline long syscall4(long num, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10_val asm("r10") = a4;
    asm volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10_val)
        : "memory"
    );
    return ret;
}

static inline long sys_write(int fd, const void *buf, unsigned long len) {
    return syscall3(SYS_WRITE, fd, (long)buf, (long)len);
}

static inline long sys_open(const char *path) {
    return syscall3(SYS_OPEN, (long)path, 0, 0);
}

static inline long sys_read(int fd, void *buf, unsigned long len) {
    return syscall3(SYS_READ, fd, (long)buf, (long)len);
}

static inline long sys_close(int fd) {
    return syscall3(SYS_CLOSE, fd, 0, 0);
}

/* new_brk == 0 just queries the current break without changing it --
 * classic brk() convention. */
static inline long sys_brk(unsigned long new_brk) {
    return syscall3(SYS_BRK, (long)new_brk, 0, 0);
}

/* returns 0 in the child, the child's pid in the parent, -1 on failure */
static inline long sys_fork(void) {
    return syscall3(SYS_FORK, 0, 0, 0);
}

/* only returns (with -1) on failure -- on success, this line of code
 * never resumes, a completely different program is running here now */
static inline long sys_execve(const char *path, char **argv) {
    return syscall3(SYS_EXECVE, (long)path, (long)argv, 0);
}

/* target_pid == -1 means "any child". blocks until a matching child
 * exits, then returns its pid (or -1 if there's no such child at all).
 * writes the child's exit code to *status if status is non-NULL. */
static inline long sys_waitpid(int target_pid, int *status) {
    return syscall3(SYS_WAITPID, target_pid, (long)status, 0);
}

static inline void sys_exit(int code) {
    syscall3(SYS_EXIT, code, 0, 0);
    for (;;) { } /* sys_exit never returns, this is just a safety net */
}

/* returns 1 and writes the index'th entry's name into name_out on
 * success, 0 once index runs past the last entry, -1 on error */
static inline long sys_readdir(const char *path, int index, char *name_out, unsigned long buf_size) {
    return syscall4(SYS_READDIR, (long)path, index, (long)name_out, (long)buf_size);
}

static inline long sys_chdir(const char *path) {
    return syscall3(SYS_CHDIR, (long)path, 0, 0);
}

static inline long sys_getcwd(char *buf, unsigned long size) {
    return syscall3(SYS_GETCWD, (long)buf, (long)size, 0);
}

static inline long sys_mkdir(const char *path) {
    return syscall3(SYS_MKDIR, (long)path, 0, 0);
}

static inline long sys_create(const char *path) {
    return syscall3(SYS_CREATE, (long)path, 0, 0);
}

static inline long sys_unlink(const char *path) {
    return syscall3(SYS_UNLINK, (long)path, 0, 0);
}

static inline long sys_stat(const char *path, stat_t *out) {
    return syscall3(SYS_STAT, (long)path, (long)out, 0);
}

static inline long sys_dup2(int oldfd, int newfd) {
    return syscall3(SYS_DUP2, oldfd, newfd, 0);
}

/* fds_out[0] = read end, fds_out[1] = write end, matching real pipe(2) */
static inline long sys_pipe(int fds_out[2]) {
    return syscall3(SYS_PIPE, (long)fds_out, 0, 0);
}
