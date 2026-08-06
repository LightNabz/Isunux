#include "mini_libc.h"
#include "mini_printf.h"

/* Exercises sys_kill()/process_terminate() across the two states a
 * target process can actually be in from the killer's point of view:
 * BLOCKED (case A, asleep in pipe_read()) and READY/RUNNING (case B, a
 * tight spin loop that never syscalls at all, only ever descheduled by
 * timer preemption). Case C checks self-kill's "never return to
 * userspace" requirement. Case D is the simple not-racy sanity check
 * that doesn't depend on timing at all. Deliberately NOT tested here:
 * kill() on an already-zombie-but-not-yet-reaped pid returning 0
 * without disturbing its real exit code -- reliably forcing that
 * ordering (child must have called sys_exit() before the parent's
 * sys_kill() fires) needs real parent/child synchronization, which is
 * more machinery than this scope-limited signal implementation is
 * worth building a test harness for right now. */

static int run_case(int case_id, const char *label, int expected_sig) {
    int status = -999;
    int pid = -1;

    if (case_id == 0) { /* A: blocked child, killed with SIGTERM */
        int fds[2];
        sys_pipe(fds);
        pid = (int)sys_fork();
        if (pid == 0) {
            sys_close(fds[1]); /* only reading -- and with the write end still open elsewhere, this blocks forever */
            char c;
            sys_read(fds[0], &c, 1); /* never returns -- killed while BLOCKED in pipe_read() */
            sys_exit(0); /* unreachable */
        }
        sys_kill(pid, SIGTERM);
        sys_waitpid(pid, &status);
        sys_close(fds[0]);
        sys_close(fds[1]);
    } else if (case_id == 1) { /* B: busy-spinning child, killed with SIGKILL */
        pid = (int)sys_fork();
        if (pid == 0) {
            for (;;) { } /* never syscalls at all -- only ever descheduled by timer preemption, killed while READY/RUNNING */
        }
        sys_kill(pid, SIGKILL);
        sys_waitpid(pid, &status);
    } else { /* C: self-kill */
        pid = (int)sys_fork();
        if (pid == 0) {
            long mypid = sys_getpid();
            sys_kill((int)mypid, SIGTERM); /* never returns */
            sys_exit(0); /* unreachable */
        }
        sys_waitpid(pid, &status);
    }

    int pass = WIFSIGNALED(status) && WTERMSIG(status) == expected_sig;
    printf("case %s: pid %d, status %d, WIFSIGNALED=%d, WTERMSIG=%d (expected sig %d) -- %s\n",
           label, pid, status, WIFSIGNALED(status), WTERMSIG(status), expected_sig, pass ? "PASS" : "FAIL");
    return pass;
}

int main(void) {
    int all_pass = 1;

    all_pass &= run_case(0, "A", SIGTERM); /* blocked child, killed with SIGTERM */
    all_pass &= run_case(1, "B", SIGKILL); /* busy-spinning child, killed with SIGKILL */
    all_pass &= run_case(2, "C", SIGTERM); /* child kills itself with SIGTERM */

    long bogus = sys_kill(9999, SIGTERM); /* pid that was never assigned */
    int case_d_pass = (bogus == -1);
    printf("case D: sys_kill on nonexistent pid returned %d (expected -1) -- %s\n",
           (int)bogus, case_d_pass ? "PASS" : "FAIL");
    all_pass &= case_d_pass;

    /* Regression test: pid 0 is the sentinel every free/unused
     * process_pool slot has after being zeroed on reap -- NOT a real
     * pid. process_find_by_pid() used to match it against any free
     * slot and hand back a garbage, all-zero process_t* (including a
     * NULL ->task), which sys_kill(0, ...) then dereferenced --
     * crashing the entire kernel (a write to task_t.state at offset
     * 0x38) from ordinary, unprivileged userland. This must return -1
     * cleanly, same as any other nonexistent pid, and the kernel must
     * obviously still be alive to print the result at all. */
    long zero_pid = sys_kill(0, SIGTERM);
    int case_e_pass = (zero_pid == -1);
    printf("case E: sys_kill on pid 0 returned %d (expected -1, and this line printing at all means the kernel survived) -- %s\n",
           (int)zero_pid, case_e_pass ? "PASS" : "FAIL");
    all_pass &= case_e_pass;

    if (all_pass) {
        printf("PASS: all signal cases behaved correctly\n");
        return 0;
    }
    printf("FAIL: at least one signal case above did not match\n");
    return 1;
}
