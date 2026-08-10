#include "mini_libc.h"
#include "mini_printf.h"

int main(int argc, char **argv) {
    /* real cat's behavior with no file operands: read stdin instead of
     * erroring out. This is what makes `cat` useful as a pipe sink
     * (`echo x | cat`) or terminal passthrough (`cat` with no args,
     * Ctrl-D to end) -- without it, cat never touches fd 0 at all. */
    if (argc < 2) {
        char buf[128];
        long n;
        while ((n = sys_read(0, buf, sizeof(buf))) > 0) {
            sys_write(1, buf, (unsigned long)n);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        long fd = sys_open(argv[i]);
        if (fd < 0) {
            printf("cat: cannot open %s\n", argv[i]);
            continue;
        }

        char buf[128];
        long n;
        while ((n = sys_read((int)fd, buf, sizeof(buf))) > 0) {
            sys_write(1, buf, (unsigned long)n);
        }
        sys_close((int)fd);
    }

    return 0;
}
