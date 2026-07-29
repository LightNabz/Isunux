#include "fork.h"
#include "task.h"
#include "process.h"
#include "vmm.h"
#include "pmm.h"
#include "kutil.h"
#include "serial.h"

#define FORK_KSTACK_PAGES 4

/* the exact point in isr.asm's syscall epilogue where a normal
 * syscall_handler() call returns to -- see isr.asm for why jumping
 * straight in here works identically for "resuming a real return" and
 * "resuming a hand-built child frame that never actually went through
 * syscall_handler at all" */
extern void syscall_return_point(void);

int64_t do_fork(interrupt_frame_t *parent_frame) {
    process_t *parent = process_current();
    if (!parent) return -1;

    process_t *child = process_alloc(parent->pid);
    if (!child) {
        serial_print("[fork] out of process slots\n");
        return -1;
    }

    uint64_t child_pml4 = vmm_new_address_space();
    vmm_clone_lower_half(child_pml4, parent->pml4_phys);
    process_clone_into(child, parent, child_pml4);

    task_t *child_task = task_alloc_raw("forked");
    if (!child_task) {
        serial_print("[fork] out of task slots\n");
        return -1;
    }
    child_task->proc = child;
    child->task = child_task;

    uint64_t kstack_phys = pmm_alloc_pages(FORK_KSTACK_PAGES);
    if (kstack_phys == 0) {
        serial_print("[fork] failed to allocate child kernel stack\n");
        return -1;
    }
    child_task->stack_phys = kstack_phys;

    uint64_t kstack_top = vmm_hhdm_offset() + kstack_phys + (FORK_KSTACK_PAGES * PAGE_SIZE);
    child_task->kernel_stack_top = kstack_top;

    uint8_t *sp = (uint8_t *)kstack_top;

    /* the full interrupt frame, copied byte-for-byte from the parent's
     * current trap -- same registers, same rip (right after `int
     * 0x80`), same user rsp/ss/rflags -- except rax, which becomes the
     * child's fork() return value: 0 */
    sp -= sizeof(interrupt_frame_t);
    interrupt_frame_t *child_frame = (interrupt_frame_t *)sp;
    *child_frame = *parent_frame;
    child_frame->rax = 0;

    /* below that: switch_context's own fake return address + 6 dummy
     * callee-saved regs -- the exact same trick task_create() uses,
     * just pointed at a spot in the middle of isr.asm instead of a
     * plain C trampoline */
    uint64_t *sp64 = (uint64_t *)sp;
    *(--sp64) = (uint64_t)syscall_return_point;
    *(--sp64) = 0; /* rbp */
    *(--sp64) = 0; /* rbx */
    *(--sp64) = 0; /* r12 */
    *(--sp64) = 0; /* r13 */
    *(--sp64) = 0; /* r14 */
    *(--sp64) = 0; /* r15 */

    child_task->rsp = (uint64_t)sp64;
    child_task->state = TASK_READY;

    return child->pid;
}
