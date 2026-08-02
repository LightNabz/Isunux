#include "task.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "kutil.h"
#include "tss.h"
#include "usermode.h"

#define MAX_TASKS        16
#define TASK_STACK_PAGES 4 /* 16 KiB per kernel-thread task */
#define USER_KSTACK_PAGES 4 /* 16 KiB dedicated kernel stack per ring-3 task */

static task_t tasks[MAX_TASKS];
static int task_count = 0;
static task_t *current_task = NULL;

extern void switch_context(uint64_t *old_rsp_ptr, uint64_t new_rsp);

static void unlink_task_from_ring(task_t *t) {
    if (t == current_task) return;

    task_t *iter = current_task;
    while (iter->next != t && iter->next != current_task) {
        iter = iter->next;
    }

    if (iter->next == t) {
        iter->next = t->next;
    }
}

static task_t *find_terminated_task(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (&tasks[i] != current_task && tasks[i].state == TASK_TERMINATED) {
            return &tasks[i];
        }
    }
    return NULL;
}

static void task_entry_trampoline(void) {
    /* A task can be launched two ways: voluntarily (via yield(), called
     * with interrupts already enabled) or preemptively (from inside the
     * timer ISR, where hardware auto-disabled interrupts on entry).
     * Either way, a freshly-started task should run with interrupts on
     * -- otherwise a task that only ever gets first scheduled via
     * preemption would silently never be preemptible itself. */
    asm volatile ("sti");

    task_t *self = current_task;
    self->entry();

    self->state = TASK_TERMINATED;
    serial_print("[task] '");
    serial_print(self->name);
    serial_print("' finished, yielding forever\n");
    for (;;) {
        yield();
    }
}

static void user_task_entry_trampoline(void) {
    asm volatile ("sti");
    task_t *self = current_task;
    /* one-way trip into ring 3 -- never returns. If the ring-3 program
     * eventually calls sys_exit, that's handled entirely inside
     * syscall.c; this function's job ends the moment enter_userspace
     * executes its iretq. */
    enter_userspace(self->user_entry_rip, self->user_entry_rsp);
}

void task_init(void) {
    task_t *main_task = &tasks[task_count++];
    main_task->name = "main";
    main_task->entry = NULL;
    main_task->proc = NULL;
    main_task->kernel_stack_top = 0; /* never consulted -- 'main' never runs ring 3 */
    main_task->state = TASK_RUNNING;
    main_task->rsp = 0; /* unused until we first switch away from it */
    main_task->next = main_task; /* ring of one, for now */

    current_task = main_task;
}

task_t *task_alloc_raw(const char *name) {
    task_t *t = NULL;

    if (task_count < MAX_TASKS) {
        t = &tasks[task_count++];
    } else {
        t = find_terminated_task();
        if (t) {
            unlink_task_from_ring(t);
            /* this slot's previous occupant is done for good -- it was
             * already unlinked from the ring above, so its kernel stack
             * will never be resumed again. Free it now, before
             * task_create()/task_create_user() allocates a brand new one
             * on top and this pointer is gone for good. */
            if (t->stack_phys && t->stack_pages) {
                pmm_free_pages(t->stack_phys, t->stack_pages);
            }
        }
    }

    if (!t) {
        serial_print("[task] out of task slots!\n");
        return NULL;
    }

    t->name = name;
    t->entry = NULL;
    t->user_entry_rip = 0;
    t->user_entry_rsp = 0;
    t->proc = NULL;
    t->kernel_stack_top = 0;
    t->state = TASK_READY;
    t->rsp = 0;
    t->stack_phys = 0;
    t->stack_pages = 0;

    /* splice into the ring right after the current task */
    t->next = current_task->next;
    current_task->next = t;

    return t;
}

task_t *task_create(const char *name, void (*entry)(void)) {
    task_t *t = task_alloc_raw(name);
    if (!t) return NULL;
    t->entry = entry;

    uint64_t stack_phys = pmm_alloc_pages(TASK_STACK_PAGES);
    if (stack_phys == 0) {
        serial_print("[task] failed to allocate a stack!\n");
        return NULL;
    }
    t->stack_phys = stack_phys;
    t->stack_pages = TASK_STACK_PAGES;

    uint64_t stack_top = vmm_hhdm_offset() + stack_phys + (TASK_STACK_PAGES * PAGE_SIZE);
    t->kernel_stack_top = stack_top; /* not consulted via TSS for a pure-kernel task, but kept consistent */

    /* Hand-craft the stack so switch_context's pops + ret land exactly
     * on task_entry_trampoline, as if this task had already been
     * switched out once before. One extra pad qword keeps rsp%16==8 at
     * the trampoline's entry, matching what a real `call` would leave --
     * the trampoline is regular compiler-generated C and expects that. */
    uint64_t *sp = (uint64_t *)stack_top;
    *(--sp) = 0;                                  /* alignment padding */
    *(--sp) = (uint64_t)task_entry_trampoline;      /* fake return address */
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* rbx */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */

    t->rsp = (uint64_t)sp;
    return t;
}

task_t *task_create_user(const char *name, process_t *proc, uint64_t entry_rip, uint64_t entry_rsp) {
    task_t *t = task_alloc_raw(name);
    if (!t) return NULL;

    t->proc = proc;
    proc->task = t;
    t->user_entry_rip = entry_rip;
    t->user_entry_rsp = entry_rsp;

    uint64_t kstack_phys = pmm_alloc_pages(USER_KSTACK_PAGES);
    if (kstack_phys == 0) {
        serial_print("[task] failed to allocate a kernel stack!\n");
        return NULL;
    }
    t->stack_phys = kstack_phys;
    t->stack_pages = USER_KSTACK_PAGES;

    uint64_t kstack_top = vmm_hhdm_offset() + kstack_phys + (USER_KSTACK_PAGES * PAGE_SIZE);
    t->kernel_stack_top = kstack_top;

    /* same fake-frame trick as task_create(), just landing on a
     * different trampoline that knows to jump into ring 3 instead of
     * calling a plain kernel function */
    uint64_t *sp = (uint64_t *)kstack_top;
    *(--sp) = 0;
    *(--sp) = (uint64_t)user_task_entry_trampoline;
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* rbx */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */

    t->rsp = (uint64_t)sp;
    return t;
}

static task_t *pick_next_ready(task_t *from) {
    task_t *t = from->next;
    while (t != from) {
        if (t->state == TASK_READY) return t;
        t = t->next;
    }
    return from; /* nobody else is runnable */
}

void yield(void) {
    task_t *prev = current_task;
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }

    task_t *next = pick_next_ready(prev);
    if (next == prev) {
        prev->state = TASK_RUNNING;
        return; /* nothing else to run, keep going */
    }

    next->state = TASK_RUNNING;
    current_task = next;

    /* CRITICAL now that more than one task can trap from ring 3: point
     * the TSS at *this* task's own kernel stack before switching to it,
     * so its next syscall/interrupt lands somewhere valid and private
     * instead of on whatever task happened to set TSS.rsp0 last. */
    tss_set_kernel_stack(next->kernel_stack_top);

    /* and switch to whichever address space this task actually belongs
     * to -- different processes have genuinely different page tables
     * now. Kernel-only tasks (proc == NULL, e.g. 'main') just run under
     * the kernel's own tables, which is always safe since the shared
     * high half is identical across every address space we ever build. */
    uint64_t next_pml4 = next->proc ? next->proc->pml4_phys : vmm_kernel_pml4();
    vmm_activate(next_pml4);

    switch_context(&prev->rsp, next->rsp);
    /* execution resumes here once something switches back to `prev` */
}

task_t *task_current(void) {
    return current_task;
}
