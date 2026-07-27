#pragma once

#define SYS_WRITE 0
#define SYS_EXIT  1
#define SYS_OPEN  2
#define SYS_READ  3
#define SYS_CLOSE 4
#define SYS_BRK   5

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

static inline void sys_exit(int code) {
    syscall3(SYS_EXIT, code, 0, 0);
    for (;;) { } /* sys_exit never returns, this is just a safety net */
}

static inline unsigned long strlen_(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}
