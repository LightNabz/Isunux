#include "syscall.h"
#include "serial.h"

/* No VFS, no file descriptors yet (that's milestone 7) -- everything
 * written just goes to serial for now. Good enough to prove the
 * ring3->ring0 round trip actually carries real data. */
static long sys_write(const char *buf, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        serial_putc(buf[i]);
    }
    return (long)len;
}

static void sys_exit(int code) {
    serial_print("[syscall] exit(");
    serial_print_dec((uint64_t)(code < 0 ? -code : code));
    serial_print(")\n");

    /* No process teardown machinery yet (freeing its address space,
     * telling a parent, etc -- that needs process bookkeeping we don't
     * have until milestone 7ish). For now: just stop cleanly instead of
     * letting a process fall off the end of its code into nothing. */
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void syscall_handler(interrupt_frame_t *frame) {
    uint64_t syscall_no = frame->rax;

    switch (syscall_no) {
        case SYS_WRITE: {
            const char *buf = (const char *)frame->rdi;
            uint64_t len = frame->rsi;
            frame->rax = (uint64_t)sys_write(buf, len);
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
