#include "mini_libc.h"
#include "mini_printf.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        const char *msg = "usage: mkdir <dir>\n";
        sys_write(2, msg, 19);
        return 1;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (sys_mkdir(argv[i]) < 0) {
            printf("mkdir: cannot create directory '%s'\n", argv[i]);
            status = 1;
        }
    }
    return status;
}
