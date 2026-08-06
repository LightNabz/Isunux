#include "mini_libc.h"
#include "mini_printf.h"

/* usage: kill <pid> [signum]   -- signum defaults to SIGTERM (15) */
int main(int argc, char **argv) {
    if (argc < 2) {
        const char *msg = "usage: kill <pid> [signum]\n";
        sys_write(2, msg, 28);
        return 1;
    }

    const char *p = argv[1];
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (*p < '0' || *p > '9') {
        printf("kill: invalid pid '%s'\n", argv[1]);
        return 1;
    }
    int pid = 0;
    while (*p >= '0' && *p <= '9') { pid = pid * 10 + (*p - '0'); p++; }
    if (*p != '\0') {
        printf("kill: invalid pid '%s'\n", argv[1]);
        return 1;
    }
    if (neg) pid = -pid;
    if (pid <= 0) {
        printf("kill: invalid pid '%s'\n", argv[1]);
        return 1;
    }

    int sig = SIGTERM;
    if (argc > 2) {
        sig = 0;
        p = argv[2];
        while (*p >= '0' && *p <= '9') { sig = sig * 10 + (*p - '0'); p++; }
    }

    if (sys_kill(pid, sig) < 0) {
        printf("kill: (%d) - no such process or unrecognized signal\n", pid);
        return 1;
    }
    return 0;
}
