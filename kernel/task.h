#pragma once
#include <stdint.h>
#include "process.h"

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_TERMINATED,
} task_state_t;

typedef struct task {
    uint64_t rsp;             /* saved stack pointer -- only valid while NOT running */
    uint64_t stack_phys;       /* base of this task's kernel stack, for bookkeeping */
    uint64_t stack_pages;      /* how many pages stack_phys spans -- needed to free it correctly on recycle */
    uint64_t kernel_stack_top; /* what TSS.rsp0 gets set to whenever this task is scheduled in */

    void (*entry)(void);      /* for plain kernel-function tasks (task_create) */

    uint64_t user_entry_rip;  /* for ring-3 tasks (task_create_user) -- read once, by the trampoline */
    uint64_t user_entry_rsp;

    task_state_t state;
    const char *name;
    process_t *proc;          /* NULL for pure-kernel tasks (e.g. 'main') */
    struct task *next;        /* circular linked list */
} task_t;

/* Turns the currently executing flow of control (whatever called this)
 * into task 0 -- the thing every other task eventually yields back to. */
void task_init(void);

/* Allocates a task_t and splices it into the round-robin ring, but
 * leaves ->rsp (and everything else initial-stack-shaped) unset --
 * callers finish the job by constructing whatever fake initial frame
 * they need. Used internally by task_create/task_create_user, and by
 * do_fork() (fork.c) to build a child task whose initial frame is a
 * full copy of the parent's interrupt frame rather than either of
 * those two shapes. */
task_t *task_alloc_raw(const char *name);

/* A task that starts by calling a plain kernel C function (milestone 5
 * style -- never touches ring 3). */
task_t *task_create(const char *name, void (*entry)(void));

/* A task that starts by launching straight into ring 3 at entry_rip
 * with the given initial user stack pointer -- owns `proc`, gets its
 * own dedicated kernel stack for syscalls/interrupts. */
task_t *task_create_user(const char *name, process_t *proc, uint64_t entry_rip, uint64_t entry_rsp);

/* Voluntarily give up the CPU to the next READY task in the ring. Also
 * what the timer IRQ calls for preemption -- see irq.c. */
void yield(void);

task_t *task_current(void);
