#include "syscall.h"
#include "serial.h"
#include "process.h"

static void sys_exit(int code) {
    serial_print("[syscall] exit(");
    serial_print_dec((uint64_t)(code < 0 ? -code : code));
    serial_print(")\n");

    /* No process teardown machinery yet (freeing its address space,
     * telling a parent, etc -- needs process bookkeeping beyond what
     * this milestone builds). For now: just stop cleanly instead of
     * letting a process fall off the end of its code into nothing. */
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void syscall_handler(interrupt_frame_t *frame) {
    uint64_t syscall_no = frame->rax;

    switch (syscall_no) {
        case SYS_WRITE: {
            int fd = (int)frame->rdi;
            const void *buf = (const void *)frame->rsi;
            uint64_t len = frame->rdx;
            frame->rax = (uint64_t)process_write(current_process, fd, buf, len);
            break;
        }
        case SYS_OPEN: {
            const char *path = (const char *)frame->rdi;
            frame->rax = (uint64_t)(long)process_open(current_process, path);
            break;
        }
        case SYS_READ: {
            int fd = (int)frame->rdi;
            void *buf = (void *)frame->rsi;
            uint64_t len = frame->rdx;
            frame->rax = (uint64_t)process_read(current_process, fd, buf, len);
            break;
        }
        case SYS_CLOSE: {
            int fd = (int)frame->rdi;
            frame->rax = (uint64_t)(long)process_close(current_process, fd);
            break;
        }
        case SYS_EXIT: {
            sys_exit((int)frame->rdi);
            break; /* unreachable -- sys_exit halts forever */
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
