#include "mini_libc.h"
#include "mini_printf.h"

/* Before the exceptions.c fix, ANY genuine unhandled fault -- including
 * one from ordinary userspace code -- halted the entire kernel
 * (hcf()). That made this exact scenario untestable: writing to
 * unmapped memory would have frozen the whole VM, test harness
 * included. Now it should cleanly terminate just the child with
 * SIGSEGV and let the parent keep running normally. */
int main(void) {
    long pid = sys_fork();
    if (pid < 0) {
        printf("segfaulttest: fork failed\n");
        return 1;
    }

    if (pid == 0) {
        /* an address nowhere near any mapped region -- code is at
         * 0x400000, heap grows from just past it, stack tops out at
         * 0x600000, mmap arena starts at 0x40000000. This is comfortably
         * in nobody's territory. */
        volatile int *bogus = (volatile int *)0x99999000UL;
        *bogus = 42; /* should fault -- not a COW case, no PTE_COW bit on a page that was never mapped at all */
        printf("[child] this line should be unreachable\n");
        sys_exit(0);
    }

    int status = 0;
    sys_waitpid((int)pid, &status);

    printf("child status: %d, WIFSIGNALED=%d, WTERMSIG=%d (expected signaled, sig %d)\n",
           status, WIFSIGNALED(status), WTERMSIG(status), SIGSEGV);

    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV) {
        printf("PASS: userspace fault terminated only the child (SIGSEGV), kernel kept running\n");
        return 0;
    }
    printf("FAIL: unexpected status\n");
    return 1;
}
