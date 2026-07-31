#include "mini_libc.h"
#include "mini_printf.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        const char *msg = "usage: cat <file>\n";
        sys_write(2, msg, 18);
        return 1;
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
