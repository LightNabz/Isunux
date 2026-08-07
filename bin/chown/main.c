#include "mini_libc.h"
#include "mini_printf.h"

/* usage: chown <uid>[:<gid>] <path> [path...]
 * No /etc/passwd exists (see things.md Tier 5), so this only ever
 * takes numeric ids -- never names. Omitting :<gid> leaves the group
 * unchanged... except this kernel's chown() always sets both fields
 * together (see process_chown), so omitting it here just re-sends
 * gid 0 -- documented scope cut, matches this project's general
 * "simplest thing that works" philosophy elsewhere. */
int main(int argc, char **argv) {
    if (argc < 3) {
        const char *msg = "usage: chown <uid>[:<gid>] <path>...\n";
        sys_write(2, msg, 38);
        return 1;
    }

    const char *p = argv[1];
    if (*p < '0' || *p > '9') {
        printf("chown: invalid owner '%s'\n", argv[1]);
        return 1;
    }
    unsigned long uid = 0;
    while (*p >= '0' && *p <= '9') { uid = uid * 10 + (unsigned long)(*p - '0'); p++; }

    unsigned long gid = 0;
    if (*p == ':') {
        p++;
        if (*p < '0' || *p > '9') {
            printf("chown: invalid group '%s'\n", argv[1]);
            return 1;
        }
        while (*p >= '0' && *p <= '9') { gid = gid * 10 + (unsigned long)(*p - '0'); p++; }
    }
    if (*p != '\0') {
        printf("chown: invalid owner spec '%s'\n", argv[1]);
        return 1;
    }

    int status = 0;
    for (int i = 2; i < argc; i++) {
        if (sys_chown(argv[i], uid, gid) < 0) {
            printf("chown: cannot change '%s'\n", argv[i]);
            status = 1;
        }
    }
    return status;
}
