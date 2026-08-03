#include "syscall.h"
#include "serial.h"
#include "process.h"
#include "task.h"
#include "fork.h"
#include "exec.h"
#include "vfs.h"

static void sys_exit(int code) {
    serial_print("[syscall] exit(");
    serial_print_dec((uint64_t)(code < 0 ? -code : code));
    serial_print(")\n");

    process_t *proc = process_current();
    task_t *self = task_current();
    if (proc) {
        process_terminate(proc, self, code);
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

/* sig is one of SIGINT/SIGKILL/SIGTERM/SIGCHLD (syscall.h). There's no
 * sigaction() yet, so every signal's action is hardcoded: INT/TERM/KILL
 * always terminate (nothing can catch, block, or ignore them -- an
 * honest scope cut, not a bug), CHLD is always ignored (its real
 * default action anyway -- the parent-wakeup half of its job is already
 * handled unconditionally by process_mark_zombie's task_wake(), whether
 * or not anyone ever sends the signal itself).
 *
 * Terminating another process synchronously, right here, instead of
 * just setting a pending-signal flag for it to notice later, is safe
 * specifically because there's exactly one CPU and a purely cooperative
 * scheduler: the only task that can possibly be executing right now is
 * this one (the caller), so `target` -- unless it's the caller itself
 * -- is definitely sitting idle (READY or BLOCKED), never mid-execution
 * somewhere unsafe to reach in and terminate. */
static long sys_kill(int target_pid, int sig) {
    process_t *self = process_current();
    process_t *target = process_find_by_pid(target_pid);
    if (!target) return -1; /* no such process */

    switch (sig) {
        case SIGKILL:
        case SIGTERM:
        case SIGINT: {
            if (target->is_zombie) return 0; /* already dead -- delivering a fatal signal to a zombie is a harmless no-op, same as real kill() */

            int encoded_exit = 128 + sig; /* real shell/wait convention: killed-by-signal N reports as exit status 128+N */

            if (target == self) {
                /* killing ourselves -- must not return to userspace at
                 * all afterward, same requirement sys_exit has, and for
                 * the same reason: our own task_t is about to be
                 * TASK_TERMINATED, and letting the syscall return
                 * normally would iretq straight back into a "dead"
                 * task's userspace code for however long it takes the
                 * next timer tick to notice. */
                process_terminate(target, task_current(), encoded_exit);
                for (;;) yield();
            }

            process_terminate(target, target->task, encoded_exit);
            return 0;
        }
        case SIGCHLD:
            return 0; /* ignored by default -- no handler mechanism exists yet for a process to react to it */
        default:
            return -1; /* unrecognized signal */
    }
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
            frame->rax = (uint64_t)(long)process_open(proc, path);
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
            frame->rax = (uint64_t)do_exec(frame, path, argv);
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
        case SYS_CREATE: {
            const char *path = (const char *)frame->rdi;
            frame->rax = (uint64_t)(int64_t)process_create(proc, path);
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
        case SYS_EXIT: {
            sys_exit((int)frame->rdi);
            break; /* unreachable -- sys_exit never returns */
        }
        default: {
            serial_print("[syscall] unknown syscall number ");
            serial_print_dec(syscall_no);
            serial_print("\n");
            frame->rax = (uint64_t)-1;
            break;
        }
    }
}
