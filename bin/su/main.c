#include "mini_libc.h"
#include "mini_printf.h"

/* usage: su <uid> [gid]
 *
 * Real su authenticates via a password and a name -> uid lookup in
 * /etc/passwd. Neither exists here yet (no login system at all --
 * see things.md's Tier 5 "permission enhancement" item), so this is
 * the bare primitive underneath that: switch this process's own
 * identity via setuid()/setgid(), then exec a fresh shell so you
 * actually get an interactive prompt running as the new identity,
 * rather than just returning to the same shell instance unchanged.
 *
 * Since there's no authentication, this only ever works when the
 * CALLER is already root -- exactly like real setuid()'s privileged
 * case. A non-root shell trying to "su" to anyone (even back to
 * itself with a different uid) is rejected by the kernel, not by
 * this program. */
int main(int argc, char **argv, char **envp) {
    if (argc < 2) {
        const char *msg = "usage: su <uid> [gid]\n";
        sys_write(2, msg, 22);
        return 1;
    }

    const char *p = argv[1];
    if (*p < '0' || *p > '9') {
        printf("su: invalid uid '%s'\n", argv[1]);
        return 1;
    }
    unsigned long uid = 0;
    while (*p >= '0' && *p <= '9') { uid = uid * 10 + (unsigned long)(*p - '0'); p++; }

    unsigned long gid = uid; /* default: same numeric id as the uid, common toy convention when no /etc/group exists to look one up */
    if (argc > 2) {
        p = argv[2];
        if (*p < '0' || *p > '9') {
            printf("su: invalid gid '%s'\n", argv[2]);
            return 1;
        }
        gid = 0;
        while (*p >= '0' && *p <= '9') { gid = gid * 10 + (unsigned long)(*p - '0'); p++; }
    }

    /* gid MUST be set before uid: once uid drops away from root, this
     * process no longer has permission to setgid() at all (see
     * process_setgid's own-uid-must-be-root check) -- same reason
     * every real privilege-dropping program does setgid() before
     * setuid(), never the other way around. */
    sys_setgid(gid); /* best-effort */
    if (sys_setuid(uid) < 0) {
        printf("su: permission denied\n");
        return 1;
    }

    char *new_argv[] = { "sh", 0 };
    sys_execve("/bin/sh", new_argv, envp); /* real su preserves the environment across a user switch (barring an explicit -/-l login-style reset, which this doesn't implement) */

    /* only reached if execve itself failed */
    printf("su: failed to exec /bin/sh\n");
    return 1;
}
