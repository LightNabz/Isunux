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
    if (proc) {
        /* real Unix closes every fd on process exit too -- without
         * this, a child holding the write end of a pipe that just
         * exits (rather than explicitly close()ing first, which is
         * the completely normal case) would never trigger EOF for
         * whoever's reading the other end, and they'd block forever. */
        for (int fd = 0; fd < MAX_FDS; fd++) {
            process_close(proc, fd);
        }
        process_mark_zombie(proc, code);
    }

    /* Address space and task slot are still not freed -- a parent's
     * waitpid() only needs the exit code and the zombie flag, not full
     * teardown. A terminated task just permanently yields the CPU
     * instead of ever being resumed again. Critically, this must NOT
     * halt the whole CPU the way it used to when there was only ever
     * one process -- now that multiple tasks coexist, that would
     * freeze every other task in the system along with this one. */
    task_t *self = task_current();
    self->state = TASK_TERMINATED;
    for (;;) {
        yield();
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
