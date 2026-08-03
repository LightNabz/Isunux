#include "mini_libc.h"
#include "mini_printf.h"

/* A plain writable global -- lives in the .data segment, which fork()
 * now COW-shares (PTE_WRITE cleared, PTE_COW set) instead of deep-
 * copying. Whichever of parent/child writes to it first should trigger
 * exception_handler's vector-14 COW path and get its own private page;
 * the other should never see that write. */
static volatile int shared_value = 100;

int main(void) {
    printf("before fork: shared_value = %d\n", shared_value);

    long pid = sys_fork();
    if (pid < 0) {
        printf("cowtest: fork failed\n");
        return 1;
    }

    if (pid == 0) {
        /* child -- this write is what should trigger the COW fault */
        shared_value = 42;
        printf("[child] set shared_value = %d (this should fault-and-copy, not corrupt the parent's page)\n", shared_value);
        sys_exit(0);
    }

    /* parent -- sys_waitpid cooperatively yields until the child (which
     * does the actual write+exit above) is done, so there's no race:
     * by the time this reads shared_value, the child's write (and its
     * COW fault, if that's what happens) has already happened. */
    int status = 0;
    sys_waitpid((int)pid, &status);

    printf("[parent] after child exited, shared_value = %d (expected 100 -- untouched)\n", shared_value);

    if (shared_value == 100) {
        printf("PASS: parent and child had independent copies after fork -- COW isolation confirmed\n");
        return 0;
    }

    printf("FAIL: parent's page was clobbered by the child's write -- COW isolation broken\n");
    return 1;
}
