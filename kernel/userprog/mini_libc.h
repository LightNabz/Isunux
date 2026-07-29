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
