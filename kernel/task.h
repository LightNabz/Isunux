#pragma once
#include <stdint.h>

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_TERMINATED,
} task_state_t;

typedef struct task {
    uint64_t rsp;        /* saved stack pointer -- only valid while NOT running */
    uint64_t stack_phys;  /* base of this task's allocated stack, for bookkeeping */
    void (*entry)(void);
    task_state_t state;
    const char *name;
    struct task *next;    /* circular linked list */
} task_t;

/* Turns the currently executing flow of control (whatever called this)
 * into task 0 -- the thing every other task eventually yields back to. */
void task_init(void);

/* Allocates a stack, hand-crafts it to look like it's mid-switch, and
 * adds the new task to the round-robin ring. Doesn't run it yet. */
task_t *task_create(const char *name, void (*entry)(void));

/* Voluntarily give up the CPU to the next READY task in the ring. */
void yield(void);

task_t *task_current(void);
