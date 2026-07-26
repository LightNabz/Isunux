#include "task.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "kutil.h"

#define MAX_TASKS        8
#define TASK_STACK_PAGES 4 /* 16 KiB per task -- plenty for simple test tasks */

static task_t tasks[MAX_TASKS];
static int task_count = 0;
static task_t *current_task = NULL;

static void task_entry_trampoline(void) {
    /* We land here via a `ret` inside switch_context, the very first
     * time a brand new task gets scheduled in. current_task was set to
     * us by whichever yield() call switched us in, so this is safe. */
    task_t *self = current_task;
    self->entry();

    /* the task function returned instead of looping forever -- retire
     * it cleanly instead of running off into undefined memory */
    self->state = TASK_TERMINATED;
    serial_print("[task] '");
    serial_print(self->name);
    serial_print("' finished, yielding forever\n");
    for (;;) {
        yield();
    }
}

extern void switch_context(uint64_t *old_rsp_ptr, uint64_t new_rsp);

void task_init(void) {
    task_t *main_task = &tasks[task_count++];
    main_task->name = "main";
    main_task->entry = NULL;
    main_task->state = TASK_RUNNING;
    main_task->rsp = 0; /* unused until we first switch away from it */
    main_task->next = main_task; /* ring of one, for now */

    current_task = main_task;
}

task_t *task_create(const char *name, void (*entry)(void)) {
    if (task_count >= MAX_TASKS) {
        serial_print("[task] out of task slots!\n");
        return NULL;
    }

    task_t *t = &tasks[task_count++];
    t->name = name;
    t->entry = entry;
    t->state = TASK_READY;

    uint64_t stack_phys = pmm_alloc_pages(TASK_STACK_PAGES);
    if (stack_phys == 0) {
        serial_print("[task] failed to allocate a stack!\n");
        return NULL;
    }
    t->stack_phys = stack_phys;

    uint64_t stack_top = vmm_hhdm_offset() + stack_phys + (TASK_STACK_PAGES * PAGE_SIZE);

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

    /* splice into the ring right after the current task */
    t->next = current_task->next;
    current_task->next = t;

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
    switch_context(&prev->rsp, next->rsp);
    /* execution resumes here once something switches back to `prev` */
}

task_t *task_current(void) {
    return current_task;
}
