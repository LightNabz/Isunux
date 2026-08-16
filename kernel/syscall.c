#include "syscall.h"
#include "serial.h"
#include "process.h"
#include "task.h"
#include "fork.h"
#include "exec.h"
#include "vfs.h"
#include "keyboard.h"
#include "kutil.h"
#include "errno.h"

static void sys_exit(int code) {
    serial_print("[syscall] exit(");
    serial_print_dec((uint64_t)(code < 0 ? -code : code));
    serial_print(")\n");

    process_t *proc = process_current();
    task_t *self = task_current();
    if (proc) {
        process_terminate(proc, self, ENCODE_EXITED(code));
    } else {
        /* no process attached to this task at all (shouldn't really
         * happen for anything that can reach a syscall, but matches the
         * original code's behavior of still stopping the task either way) */
        self->state = TASK_TERMINATED;
    }

    /* Address space and task slot are still not freed here -- a
     * parent's waitpid() only needs the exit code and the zombie flag,
     * not full teardown (that happens on reap, in process_waitpid). A
     * terminated task just permanently yields the CPU instead of ever
     * being resumed again. Critically, this must NOT halt the whole CPU
     * the way it used to when there was only ever one process -- now
     * that multiple tasks coexist, that would freeze every other task
     * in the system along with this one. */
    for (;;) {
        yield();
    }
}

/* sig is one of SIGINT/SIGKILL/SIGTERM/SIGCHLD/SIGTSTP/SIGCONT
 * (syscall.h). There's no sigaction() yet, so every signal's action is
 * hardcoded -- see process_send_signal's doc comment in process.h for
 * exactly what each one does and why terminating/stopping another
 * process synchronously, right here, is safe on this single-CPU
 * cooperative scheduler. */
static long sys_kill(int target_pid, int sig) {
    process_t *target = process_find_by_pid(target_pid);
    if (!target) return -ESRCH;

    int action = process_send_signal(target, sig);
    if (action < 0) return -EINVAL; /* unrecognized signal */
    if (action == 1) { for (;;) yield(); } /* killed ourselves -- never return to userspace, same requirement sys_exit() has */
    if (action == 2) { yield(); } /* stopped ourselves -- parks here; resumes right after this once some later SIGCONT sets us back to TASK_READY */
    return 0;
}

void syscall_handler(interrupt_frame_t *frame) {
    uint64_t syscall_no = frame->rax;
    process_t *proc = process_current();

    switch (syscall_no) {
        case SYS_WRITE: {
            int fd = (int)frame->rdi;
            const void *buf = (const void *)frame->rsi;
            uint64_t len = frame->rdx;
            frame->rax = (uint64_t)process_write(proc, fd, buf, len);
            break;
        }
        case SYS_OPEN: {
            const char *path = (const char *)frame->rdi;
            int flags = (int)frame->rsi;
            uint64_t mode = frame->rdx;
            frame->rax = (uint64_t)(long)process_open(proc, path, flags, mode);
            break;
        }
        case SYS_READ: {
            int fd = (int)frame->rdi;
            void *buf = (void *)frame->rsi;
            uint64_t len = frame->rdx;
            frame->rax = (uint64_t)process_read(proc, fd, buf, len);
            break;
        }
        case SYS_CLOSE: {
            int fd = (int)frame->rdi;
            frame->rax = (uint64_t)(long)process_close(proc, fd);
            break;
        }
        case SYS_BRK: {
            frame->rax = process_brk(proc, frame->rdi);
            break;
        }
        case SYS_FORK: {
            frame->rax = (uint64_t)do_fork(frame);
            break;
        }
        case SYS_EXECVE: {
            const char *path = (const char *)frame->rdi;
            char **argv = (char **)frame->rsi;
            char **envp = (char **)frame->rdx;
            frame->rax = (uint64_t)do_exec(frame, path, argv, envp);
            break;
        }
        case SYS_WAITPID: {
            int target_pid = (int)frame->rdi;
            int *status_out = (int *)frame->rsi;
            frame->rax = (uint64_t)process_waitpid(proc, target_pid, status_out);
            break;
        }
        case SYS_READDIR: {
            const char *path = (const char *)frame->rdi;
            int index = (int)frame->rsi;
            char *name_out = (char *)frame->rdx;
            uint64_t buf_size = frame->r10;
            frame->rax = (uint64_t)(int64_t)vfs_readdir_path(proc->cwd, path, index, name_out, buf_size);
            break;
        }
        case SYS_CHDIR: {
            const char *path = (const char *)frame->rdi;
            frame->rax = (uint64_t)(int64_t)process_chdir(proc, path);
            break;
        }
        case SYS_GETCWD: {
            char *buf = (char *)frame->rdi;
            uint64_t size = frame->rsi;
            uint64_t i = 0;
            for (; proc->cwd[i] && i + 1 < size; i++) buf[i] = proc->cwd[i];
            buf[i] = '\0';
            frame->rax = i;
            break;
        }
        case SYS_MKDIR: {
            const char *path = (const char *)frame->rdi;
            frame->rax = (uint64_t)(int64_t)process_mkdir(proc, path);
            break;
        }
        case SYS_UNLINK: {
            const char *path = (const char *)frame->rdi;
            frame->rax = (uint64_t)(int64_t)process_unlink(proc, path);
            break;
        }
        case SYS_STAT: {
            const char *path = (const char *)frame->rdi;
            vfs_stat_t *out = (vfs_stat_t *)frame->rsi;
            frame->rax = (uint64_t)(int64_t)process_stat(proc, path, out);
            break;
        }
        case SYS_DUP2: {
            int oldfd = (int)frame->rdi;
            int newfd = (int)frame->rsi;
            frame->rax = (uint64_t)(int64_t)process_dup2(proc, oldfd, newfd);
            break;
        }
        case SYS_PIPE: {
            int *fds_out = (int *)frame->rdi;
            int fds[2];
            int ret = process_pipe(proc, fds);
            if (ret == 0) {
                fds_out[0] = fds[0];
                fds_out[1] = fds[1];
            }
            frame->rax = (uint64_t)(int64_t)ret;
            break;
        }
        case SYS_MMAP: {
            uint64_t addr_hint = frame->rdi;
            uint64_t length = frame->rsi;
            int prot = (int)frame->rdx;
            int flags = (int)frame->r10;
            int fd = (int)frame->r8;
            uint64_t offset = frame->r9;
            frame->rax = process_mmap(proc, addr_hint, length, prot, flags, fd, offset);
            break;
        }
        case SYS_MUNMAP: {
            uint64_t addr = frame->rdi;
            uint64_t length = frame->rsi;
            frame->rax = (uint64_t)(int64_t)process_munmap(proc, addr, length);
            break;
        }
        case SYS_SET_FOREGROUND: {
            process_set_foreground((const int *)frame->rdi, (int)frame->rsi);
            frame->rax = 0;
            break;
        }
        case SYS_KILL: {
            int target_pid = (int)frame->rdi;
            int sig = (int)frame->rsi;
            frame->rax = (uint64_t)(int64_t)sys_kill(target_pid, sig);
            break;
        }
        case SYS_GETPID: {
            frame->rax = (uint64_t)(int64_t)(proc ? proc->pid : -1);
            break;
        }
        case SYS_CHMOD: {
            const char *path = (const char *)frame->rdi;
            uint64_t mode = frame->rsi;
            frame->rax = (uint64_t)(int64_t)process_chmod(proc, path, mode);
            break;
        }
        case SYS_CHOWN: {
            const char *path = (const char *)frame->rdi;
            uint64_t uid = frame->rsi;
            uint64_t gid = frame->rdx;
            frame->rax = (uint64_t)(int64_t)process_chown(proc, path, uid, gid);
            break;
        }
        case SYS_SETUID: {
            frame->rax = (uint64_t)(int64_t)process_setuid(proc, frame->rdi);
            break;
        }
        case SYS_SETGID: {
            frame->rax = (uint64_t)(int64_t)process_setgid(proc, frame->rdi);
            break;
        }
        case SYS_GETUID: {
            frame->rax = proc ? proc->uid : 0;
            break;
        }
        case SYS_GETGID: {
            frame->rax = proc ? proc->gid : 0;
            break;
        }
        case SYS_ARCH_PRCTL: {
            /* Both musl and glibc's _start call this almost immediately
             * to set up TLS -- %fs-relative memory (errno, the TLS
             * block) doesn't work at all until this has run once. */
            long code = (long)frame->rdi;
            uint64_t addr = frame->rsi;
            if (code == ARCH_SET_FS) {
                if (proc) proc->fs_base = addr;
                wrmsr(MSR_FS_BASE, addr); /* take effect immediately, same reasoning as exec.c's reset -- this task keeps running without a context switch in between */
                frame->rax = 0;
            } else if (code == ARCH_GET_FS) {
                /* addr here is actually an OUT pointer (arch_prctl(ARCH_GET_FS, &result)),
                 * not a value -- matches real Linux's arch_prctl(2) signature exactly.
                 * Trusted as-is, same as every other user pointer this kernel
                 * dereferences directly (sys_read/write, etc.) -- no validation
                 * layer exists yet anywhere in this codebase to be consistent with. */
                *(uint64_t *)addr = proc ? proc->fs_base : 0;
                frame->rax = 0;
            } else {
                frame->rax = (uint64_t)-EINVAL; /* unrecognized code -- ARCH_SET_GS/ARCH_GET_GS et al exist in real Linux but nothing here uses %gs for anything, so they're just not implemented */
            }
            break;
        }
        case SYS_TTY_SET_RAW: {
            tty_set_raw((int)frame->rdi);
            frame->rax = 0;
            break;
        }
        case SYS_TTY_SET_ECHO: {
            tty_set_echo((int)frame->rdi);
            frame->rax = 0;
            break;
        }
        case SYS_SET_TID_ADDRESS: {
            /* musl's _start calls this unconditionally even for a
             * single-threaded program -- there's no thread concept
             * below "process" on this kernel for it to actually mean
             * anything, so this is a real no-op, not a stub standing in
             * for missing functionality. Real set_tid_address() returns
             * the caller's own tid; a process's tid and pid are the
             * same number here (see SYS_GETPID), so that's what goes back. */
            frame->rax = (uint64_t)(proc ? proc->pid : 0);
            break;
        }
        case SYS_SET_ROBUST_LIST: {
            /* Same reasoning as SYS_SET_TID_ADDRESS -- musl's _start
             * calls this too, and the robust-futex-list it's registering
             * only matters if a thread can die holding a lock another
             * thread is waiting on, which needs real threads to happen
             * at all. Real set_robust_list() returns 0 on success. */
            frame->rax = 0;
            break;
        }
        case SYS_EXIT: {
            sys_exit((int)frame->rdi);
            break; /* unreachable -- sys_exit never returns */
        }
        case SYS_EXIT_GROUP: {
            /* Distinct from exit() in real Linux (one thread vs the
             * whole process) -- identical here, since every task on
             * this kernel already IS a whole process, there's no
             * "one thread of a process" for the two to actually differ
             * on. musl's _start calls exit_group, not exit, so this
             * needs its own case even though the handler is the same. */
            sys_exit((int)frame->rdi);
            break;
        }
        default: {
            serial_print("[syscall] unknown syscall number ");
            serial_print_dec(syscall_no);
            serial_print("\n");
            frame->rax = (uint64_t)-ENOSYS;
            break;
        }
    }
}
