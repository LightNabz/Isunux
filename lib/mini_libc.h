#pragma once

/* Real Linux x86_64 syscall numbers -- mirrors kernel/syscall.h exactly,
 * see there for the full reasoning (1f of the syscall-compat sequence). */
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
#define SYS_WAITPID        61
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
#define SYS_READDIR        217
#define SYS_EXIT_GROUP     231

/* ISUNUX-native extensions, deliberately out-of-band -- see
 * kernel/syscall.h's doc comment for why these can't be real numbers. */
#define SYS_SET_FOREGROUND 1000
#define SYS_TTY_SET_RAW    1001
#define SYS_TTY_SET_ECHO   1002

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

/* Real Linux open(2) flag values -- mirrors kernel/fcntl.h. */
#define O_ACCMODE 0x0003
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040
#define O_EXCL    0x0080
#define O_TRUNC   0x0200
#define O_APPEND  0x0400

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

/* Mirrors kernel/errno.h -- real Linux x86_64 errno values. A syscall
 * wrapper below returning a negative number in (-4096, -1] means
 * failure with that errno, e.g. sys_open() returning -ENOENT. These
 * are just the named constants for checking against that -- there's no
 * per-thread errno GLOBAL here (no `errno` variable that gets silently
 * set on every call, no thread-local storage for it) the way real libc
 * has. Check the syscall wrapper's own return value directly instead:
 * `long r = sys_open(path); if (r == -ENOENT) ...`. Real errno
 * semantics are Tier 4 territory, not needed for what this milestone
 * is actually about (see kernel/errno.h's own doc comment). */
#define EPERM     1
#define ENOENT    2
#define ESRCH     3
#define EIO       5
#define ENOEXEC   8
#define EBADF     9
#define ECHILD    10
#define EAGAIN    11
#define ENOMEM    12
#define EACCES    13
#define ENODEV    19
#define EEXIST    17
#define ENOTDIR   20
#define EISDIR    21
#define EINVAL    22
#define ENFILE    23
#define EMFILE    24
#define ENOSPC    28
#define EROFS     30
#define ENOSYS    38
#define ENOTEMPTY 39
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
 * number in rax, up to 6 args in rdi/rsi/rdx/r10/r8/r9 -- the real
 * x86_64 `syscall` ABI, triggered via the actual `syscall` instruction
 * (kernel/isr.asm's syscall_entry). `syscall` itself clobbers rcx
 * (loaded with the return address) and r11 (loaded with RFLAGS) as a
 * hardware side effect -- both need to be in the clobber list so the
 * compiler doesn't keep anything live there across the call, same as
 * real libc's syscall wrapper does. rcx isn't available for a 4th+
 * argument for exactly this reason (it's already spoken for), which is
 * why the ABI moves to r10/r8/r9 instead of the calling-convention
 * rcx/r8/r9 a plain function call would use for args 4-6.
 *
 * One function per arg count rather than a single variadic-looking
 * macro -- each needs a different exact set of register constraints,
 * and being explicit here is more honest about what's actually
 * happening than hiding it behind macro-generated boilerplate. */

static inline long syscall0(long num) {
    long ret;
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall1(long num, long a1) {
    long ret;
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall2(long num, long a1, long a2) {
    long ret;
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall4(long num, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10_val asm("r10") = a4;
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10_val)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall5(long num, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10_val asm("r10") = a4;
    register long r8_val asm("r8") = a5;
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10_val), "r"(r8_val)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10_val asm("r10") = a4;
    register long r8_val asm("r8") = a5;
    register long r9_val asm("r9") = a6;
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10_val), "r"(r8_val), "r"(r9_val)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long sys_write(int fd, const void *buf, unsigned long len) {
    return syscall3(SYS_WRITE, fd, (long)buf, (long)len);
}

static inline long sys_open(const char *path) {
    return syscall3(SYS_OPEN, (long)path, O_RDONLY, 0);
}

/* The real open(2) signature -- needed for anything that wants to
 * write, create, truncate, or append. Plain sys_open() above is just
 * this with flags=O_RDONLY, kept as its own function since almost
 * every existing caller only ever wanted read-only access anyway and
 * "open a path" reads better than "open a path with zero flags". */
static inline long sys_open3(const char *path, int flags, unsigned long mode) {
    return syscall3(SYS_OPEN, (long)path, flags, (long)mode);
}

static inline long sys_read(int fd, void *buf, unsigned long len) {
    return syscall3(SYS_READ, fd, (long)buf, (long)len);
}

static inline long sys_close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

/* new_brk == 0 just queries the current break without changing it --
 * classic brk() convention. */
static inline long sys_brk(unsigned long new_brk) {
    return syscall1(SYS_BRK, (long)new_brk);
}

/* returns 0 in the child, the child's pid in the parent, -1 on failure */
static inline long sys_fork(void) {
    return syscall0(SYS_FORK);
}

/* only returns (with -1) on failure -- on success, this line of code
 * never resumes, a completely different program is running here now.
 * envp can be NULL (treated as an empty environment, same as passing
 * an array containing just a NULL) -- real execve() requires an actual
 * envp argument, but making it optional here matches how forgiving
 * this kernel's other syscalls already are (e.g. sys_open's O_* flags
 * being similarly relaxed). */
static inline long sys_execve(const char *path, char **argv, char **envp) {
    return syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp);
}

/* target_pid == -1 means "any child". blocks until a matching child
 * exits, then returns its pid (or -1 if there's no such child at all).
 * writes the child's exit code to *status if status is non-NULL. */
static inline long sys_waitpid(int target_pid, int *status) {
    return syscall2(SYS_WAITPID, target_pid, (long)status);
}

static inline void sys_exit(int code) {
    syscall1(SYS_EXIT, code);
    for (;;) { } /* sys_exit never returns, this is just a safety net */
}

/* returns 1 and writes the index'th entry's name into name_out on
 * success, 0 once index runs past the last entry, -1 on error */
static inline long sys_readdir(const char *path, int index, char *name_out, unsigned long buf_size) {
    return syscall4(SYS_READDIR, (long)path, index, (long)name_out, (long)buf_size);
}

static inline long sys_chdir(const char *path) {
    return syscall1(SYS_CHDIR, (long)path);
}

static inline long sys_getcwd(char *buf, unsigned long size) {
    return syscall2(SYS_GETCWD, (long)buf, (long)size);
}

static inline long sys_mkdir(const char *path) {
    return syscall1(SYS_MKDIR, (long)path);
}

/* There's no SYS_CREATE syscall number anymore -- real programs create
 * files via open(O_CREAT), same as this now does. O_EXCL (not
 * O_TRUNC) is what preserves this function's ORIGINAL contract: fails
 * with -EEXIST if the name is already taken, rather than real creat()'s
 * always-succeeds-and-truncates behavior -- existing callers (bin/touch,
 * for instance) specifically rely on the fail-if-exists behavior, so
 * this keeps their meaning unchanged even though the mechanism
 * underneath is completely different now. The fd is closed before
 * returning -- callers only ever checked this for success/failure, never
 * used the return value as an fd, so leaving one open here would just
 * leak it. */
static inline long sys_create(const char *path) {
    long fd = sys_open3(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd >= 0) syscall1(SYS_CLOSE, fd);
    return fd;
}

static inline long sys_unlink(const char *path) {
    return syscall1(SYS_UNLINK, (long)path);
}

static inline long sys_stat(const char *path, stat_t *out) {
    return syscall2(SYS_STAT, (long)path, (long)out);
}

static inline long sys_dup2(int oldfd, int newfd) {
    return syscall2(SYS_DUP2, oldfd, newfd);
}

/* fds_out[0] = read end, fds_out[1] = write end, matching real pipe(2) */
static inline long sys_pipe(int fds_out[2]) {
    return syscall1(SYS_PIPE, (long)fds_out);
}

/* sig is one of SIGINT/SIGKILL/SIGTERM/SIGCHLD above. Returns 0 on
 * success (including "target_pid was already dead" -- matches real
 * kill()'s behavior of that being a harmless no-op signal delivery to
 * a zombie), -1 if target_pid doesn't exist or sig isn't recognized. */
static inline long sys_kill(int target_pid, int sig) {
    return syscall2(SYS_KILL, target_pid, sig);
}

static inline long sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

/* Only the owning uid or root may chmod a path -- returns -1 on a bad
 * path or a permission denial. mode is a raw rwxrwxrwx bitmask, e.g.
 * 0755 or 0644 (there's no umask, so whatever's passed is exactly
 * what's set). */
static inline long sys_chmod(const char *path, unsigned long mode) {
    return syscall2(SYS_CHMOD, (long)path, (long)mode);
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
    return syscall1(SYS_SETUID, (long)new_uid);
}
static inline long sys_setgid(unsigned long new_gid) {
    return syscall1(SYS_SETGID, (long)new_gid);
}

static inline long sys_getuid(void) {
    return syscall0(SYS_GETUID);
}
static inline long sys_getgid(void) {
    return syscall0(SYS_GETGID);
}

/* Switches the console between canonical (line-buffered, default) and
 * raw (every keystroke delivered immediately) input. Global, not
 * per-fd -- see keyboard.h's doc comment on tty_set_raw() for why.
 * Ctrl-C/Ctrl-Z keep working in either mode. */
static inline long sys_tty_set_raw(int enable) {
    return syscall1(SYS_TTY_SET_RAW, (long)enable);
}

/* Toggles local echo, independent of canonical/raw mode -- canonical
 * mode with echo off is a real, useful combination (the traditional
 * shape of a password-style prompt: line editing still works, nothing
 * is drawn). See keyboard.h's doc comment on tty_set_echo(). */
static inline long sys_tty_set_echo(int enable) {
    return syscall1(SYS_TTY_SET_ECHO, (long)enable);
}

/* addr_hint is always ignored (no MAP_FIXED support) -- this kernel
 * always places the mapping itself, which real mmap() explicitly
 * allows when MAP_FIXED isn't set. flags must include MAP_ANONYMOUS;
 * anything else fails, there being no file-backed mapping support yet.
 * Returns the mapped address, or -1 on failure (out of memory, or a
 * flags/length that isn't supported) -- real mmap() returns
 * (void*)-1 = MAP_FAILED for the same case, same bit pattern either way. */
static inline long sys_mmap(unsigned long addr_hint, unsigned long length, int prot, int flags) {
    /* fd=-1, offset=0 -- the only combination this kernel's mmap()
     * accepts anyway (anonymous-only, see process_mmap()'s doc comment
     * in process.h). Explicit here rather than leaving r8/r9 to whatever
     * garbage happened to be in those registers, now that the kernel
     * dispatch actually reads them. */
    return syscall6(SYS_MMAP, (long)addr_hint, (long)length, prot, flags, -1, 0);
}

static inline long sys_munmap(unsigned long addr, unsigned long length) {
    return syscall2(SYS_MUNMAP, (long)addr, (long)length);
}

/* Tells the kernel which pids should receive SIGINT/SIGTSTP on
 * Ctrl-C/Ctrl-Z (see kernel/keyboard.c) -- this kernel's stand-in for a
 * real controlling-terminal/process-group mechanism. count is clamped
 * to 8 pids; pass count 0 (pids can be NULL then) to clear it, which
 * the shell does once it's back at the prompt. */
static inline long sys_set_foreground(const int *pids, int count) {
    return syscall2(SYS_SET_FOREGROUND, (long)pids, count);
}

/* Not something mini_libc's own crt0/programs ever call themselves --
 * ISUNUX's native binaries have no TLS mechanism and don't need one.
 * This exists purely so a borrowed static musl/glibc binary's _start
 * has something real to call (see kernel/syscall.c's SYS_ARCH_PRCTL
 * case) once 1g gets here. */
static inline long sys_arch_prctl(int code, unsigned long addr) {
    return syscall2(SYS_ARCH_PRCTL, code, (long)addr);
}

/* Storage lives in crt0.asm, populated from the initial stack's envp
 * before main() is ever called -- see the doc comment there. Standard
 * Unix convention: getenv() below is just a linear scan over this,
 * exactly what real libc's own getenv() does too. */
extern char **environ;

/* NULL if the variable isn't set, or name is empty/contains '=' (both
 * invalid per real getenv()'s own rules). No setenv()/putenv() yet --
 * nothing in this codebase needs to WRITE the environment from C code
 * yet (the shell modifies its own separate table and rebuilds a fresh
 * envp[] at exec time instead, see bin/sh/main.c's build_envp_array()),
 * so there's nothing here to keep in sync with a mutable environ. */
static inline char *getenv(const char *name) {
    if (!name || !*name) return 0;
    for (int i = 0; name[i]; i++) if (name[i] == '=') return 0;

    if (!environ) return 0;
    for (int i = 0; environ[i]; i++) {
        char *entry = environ[i];
        int j = 0;
        while (name[j] && entry[j] == name[j]) j++;
        if (name[j] == '\0' && entry[j] == '=') return &entry[j + 1];
    }
    return 0;
}
