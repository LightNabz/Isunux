#include "mini_libc.h"
#include "mini_printf.h"

/* No timestamps exist anywhere in this filesystem yet, so "touch" can
 * only do half its real job: create the file if it's missing. If it
 * already exists, sys_create() fails (name taken) and that's fine --
 * touching an existing file is a silent no-op here, not an error. */
int main(int argc, char **argv) {
    if (argc < 2) {
        const char *msg = "usage: touch <file>\n";
        sys_write(2, msg, 21);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        stat_t st;
        if (sys_stat(argv[i], &st) == 0) continue; /* already exists -- nothing to do */

        if (sys_create(argv[i]) < 0) {
            printf("touch: cannot create '%s'\n", argv[i]);
            return 1;
        }
    }
    return 0;
}
