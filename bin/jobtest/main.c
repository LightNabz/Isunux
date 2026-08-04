#include "mini_libc.h"
#include "mini_printf.h"

/* The keyboard-driven half of job control (Ctrl-Z/Ctrl-C detection,
 * the foreground pid set) can't be exercised from a userland test
 * program -- there's no syscall to inject a synthetic keypress. This
 * tests the underlying kernel mechanism directly instead, the same way
 * signaltest tests SIGKILL/SIGTERM without needing a keyboard: send
 * SIGTSTP/SIGCONT via sys_kill() exactly as keyboard.c's
 * process_signal_foreground() would, and verify the state transitions
 * are correct. */
int main(void) {
    long pid = sys_fork();
    if (pid == 0) {
        for (;;) { } /* never syscalls at all -- only ever descheduled by timer preemption */
    }

    sys_kill((int)pid, SIGTSTP);
    int status = 0;
    sys_waitpid((int)pid, &status);
    int stop_ok = WIFSTOPPED(status) && WSTOPSIG(status) == SIGTSTP;
    printf("after SIGTSTP: status %d, WIFSTOPPED=%d, WSTOPSIG=%d -- %s\n",
           status, WIFSTOPPED(status), WSTOPSIG(status), stop_ok ? "PASS" : "FAIL");

    sys_kill((int)pid, SIGCONT);

    /* clean termination now, and confirm it was reapable -- a
     * genuinely-dead or already-reaped pid wouldn't be waitable at all,
     * so this also proves the child was alive (just suspended) the
     * whole time between the two waitpid() calls */
    sys_kill((int)pid, SIGTERM);
    status = 0;
    long reaped = sys_waitpid((int)pid, &status);
    int term_ok = (reaped == pid) && WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM;
    printf("after SIGCONT+SIGTERM: reaped %d, status %d, WIFSIGNALED=%d, WTERMSIG=%d -- %s\n",
           (int)reaped, status, WIFSIGNALED(status), WTERMSIG(status), term_ok ? "PASS" : "FAIL");

    if (stop_ok && term_ok) {
        printf("PASS: SIGTSTP/SIGCONT mechanism works correctly\n");
        return 0;
    }
    printf("FAIL: job control mechanism did not behave as expected\n");
    return 1;
}
