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
#define SYS_KILL    18
#define SYS_GETPID  19
#define SYS_MMAP    20
#define SYS_MUNMAP  21
#define SYS_SET_FOREGROUND 22
#define SYS_CHMOD   23
#define SYS_CHOWN   24
#define SYS_SETUID  25
#define SYS_SETGID  26
#define SYS_GETUID  27
#define SYS_GETGID  28
#define SYS_TTY_SET_RAW  29
#define SYS_TTY_SET_ECHO 30

/* Mirrors kernel/syscall.h -- see there for why only these three exist. */
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#define MAP_ANONYMOUS 0x20

/* Mirrors kernel/syscall.h exactly -- real Linux signal numbers, kept
 * unchanged on purpose. Only these four exist; all four have a fixed
 * default action (no sigaction()/handler support yet, so nothing can
 * catch, block, or ignore any of them -- SIGCHLD's default action
 * really is "ignore", not "unimplemented"). */
#define SIGINT  2
#define SIGSEGV 11
#define SIGKILL 9
#define SIGTERM 15
#define SIGCHLD 17
#define SIGTSTP 20
#define SIGCONT 18

/* Mirrors kernel/syscall.h's ENCODE_* macros -- real POSIX wait-status
 * bit layout. These are the decode side: what a caller of sys_waitpid()
 * actually inspects a status value with. */
#define WIFEXITED(status)    (((status) & 0x7f) == 0)
#define WEXITSTATUS(status)  (((status) >> 8) & 0xff)
#define WTERMSIG(status)     ((status) & 0x7f)
#define WIFSIGNALED(status)  (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WIFSTOPPED(status)   (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status)     (((status) >> 8) & 0xff)

/* Mirrors vfs_stat_t in kernel/vfs.h exactly -- this is the
 * kernel/userland ABI, kept in sync by hand like the SYS_* numbers
 * above. type is VNODE_FILE(0) or VNODE_DIR(1). */
typedef struct {
    unsigned long type;
    unsigned long size;
    unsigned long uid;
    unsigned long gid;
    unsigned long mode;
} stat_t;
#define VNODE_FILE_T 0
#define VNODE_DIR_T  1

/* Mirrors kernel/vfs.h's VFS_PERM_* exactly -- same r=4/w=2/x=1 bits as
 * a real Unix mode_t, combinable with '|' and usable directly as a
 * chmod() mode argument (e.g. 0755, 0644). */
#define VFS_PERM_READ  0x4
#define VFS_PERM_WRITE 0x2
#define VFS_PERM_EXEC  0x1

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

/* sig is one of SIGINT/SIGKILL/SIGTERM/SIGCHLD above. Returns 0 on
 * success (including "target_pid was already dead" -- matches real
 * kill()'s behavior of that being a harmless no-op signal delivery to
 * a zombie), -1 if target_pid doesn't exist or sig isn't recognized. */
static inline long sys_kill(int target_pid, int sig) {
    return syscall3(SYS_KILL, target_pid, sig, 0);
}

static inline long sys_getpid(void) {
    return syscall3(SYS_GETPID, 0, 0, 0);
}

/* Only the owning uid or root may chmod a path -- returns -1 on a bad
 * path or a permission denial. mode is a raw rwxrwxrwx bitmask, e.g.
 * 0755 or 0644 (there's no umask, so whatever's passed is exactly
 * what's set). */
static inline long sys_chmod(const char *path, unsigned long mode) {
    return syscall3(SYS_CHMOD, (long)path, (long)mode, 0);
}

/* Root-only -- gives a path a new owning uid/gid. Returns -1 on a bad
 * path or if the caller isn't root. */
static inline long sys_chown(const char *path, unsigned long uid, unsigned long gid) {
    return syscall3(SYS_CHOWN, (long)path, (long)uid, (long)gid);
}

/* Changes the calling process's own uid/gid. Only root can become
 * someone else (real setuid()'s privileged case) -- there's no
 * login/password system yet, so this is purely how a root shell can
 * drop to a lesser uid for testing. Returns -1 if the caller isn't
 * root and new_uid/gid isn't already the caller's own. */
static inline long sys_setuid(unsigned long new_uid) {
    return syscall3(SYS_SETUID, (long)new_uid, 0, 0);
}
static inline long sys_setgid(unsigned long new_gid) {
    return syscall3(SYS_SETGID, (long)new_gid, 0, 0);
}

static inline long sys_getuid(void) {
    return syscall3(SYS_GETUID, 0, 0, 0);
}
static inline long sys_getgid(void) {
    return syscall3(SYS_GETGID, 0, 0, 0);
}

/* Switches the console between canonical (line-buffered, default) and
 * raw (every keystroke delivered immediately) input. Global, not
 * per-fd -- see keyboard.h's doc comment on tty_set_raw() for why.
 * Ctrl-C/Ctrl-Z keep working in either mode. */
static inline long sys_tty_set_raw(int enable) {
    return syscall3(SYS_TTY_SET_RAW, (long)enable, 0, 0);
}

/* Toggles local echo, independent of canonical/raw mode -- canonical
 * mode with echo off is a real, useful combination (the traditional
 * shape of a password-style prompt: line editing still works, nothing
 * is drawn). See keyboard.h's doc comment on tty_set_echo(). */
static inline long sys_tty_set_echo(int enable) {
    return syscall3(SYS_TTY_SET_ECHO, (long)enable, 0, 0);
}

/* addr_hint is always ignored (no MAP_FIXED support) -- this kernel
 * always places the mapping itself, which real mmap() explicitly
 * allows when MAP_FIXED isn't set. flags must include MAP_ANONYMOUS;
 * anything else fails, there being no file-backed mapping support yet.
 * Returns the mapped address, or -1 on failure (out of memory, or a
 * flags/length that isn't supported) -- real mmap() returns
 * (void*)-1 = MAP_FAILED for the same case, same bit pattern either way. */
static inline long sys_mmap(unsigned long addr_hint, unsigned long length, int prot, int flags) {
    return syscall4(SYS_MMAP, (long)addr_hint, (long)length, prot, flags);
}

static inline long sys_munmap(unsigned long addr, unsigned long length) {
    return syscall3(SYS_MUNMAP, (long)addr, (long)length, 0);
}

/* Tells the kernel which pids should receive SIGINT/SIGTSTP on
 * Ctrl-C/Ctrl-Z (see kernel/keyboard.c) -- this kernel's stand-in for a
 * real controlling-terminal/process-group mechanism. count is clamped
 * to 8 pids; pass count 0 (pids can be NULL then) to clear it, which
 * the shell does once it's back at the prompt. */
static inline long sys_set_foreground(const int *pids, int count) {
    return syscall3(SYS_SET_FOREGROUND, (long)pids, count, 0);
}
