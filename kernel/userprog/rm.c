#include "mini_libc.h"
#include "mini_printf.h"

/* One unlink syscall handles both files and empty directories (see
 * the comment on vnode_ops.unlink in kernel/vfs.h) -- real Unix splits
 * this into rm/rmdir, but there's no reason this toy shell needs two
 * different programs for it yet. */
int main(int argc, char **argv) {
    if (argc < 2) {
        const char *msg = "usage: rm <file>\n";
        sys_write(2, msg, 17);
        return 1;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (sys_unlink(argv[i]) < 0) {
            printf("rm: cannot remove '%s'\n", argv[i]);
            status = 1;
        }
    }
    return status;
}
