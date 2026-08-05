#include "mini_libc.h"
#include "mini_printf.h"

/* Prints a heartbeat forever, with a busy-wait delay between each one --
 * there's no sleep() syscall in this kernel yet. Meant for interactively
 * testing job control: run it in the foreground and press Ctrl-Z, or
 * run it with '&' and use jobs/fg/bg. Heartbeats visibly stopping is
 * the confirmation a stop actually took effect; them resuming is the
 * confirmation a continue did too -- something segfaulttest/jobtest
 * can't demonstrate since they finish before you could ever catch them
 * mid-run with a keypress. */
int main(void) {
    for (long n = 0; ; n++) {
        printf("heartbeat %d\n", (int)n);
        for (volatile long i = 0; i < 200000000; i++) { } /* volatile -- otherwise -O2 has every right to notice this loop has no observable effect and delete it entirely */
    }
    return 0;
}
