#include "mini_libc.h"
#include "mini_printf.h"

int main(int argc, char **argv) {
    const char *path;
    char cwd_buf[128];

    if (argc > 1) {
        path = argv[1];
    } else {
        sys_getcwd(cwd_buf, sizeof(cwd_buf));
        path = cwd_buf;
    }

    char name[64];
    int any = 0;
    for (int i = 0; ; i++) {
        long found = sys_readdir(path, i, name, sizeof(name));
        if (found <= 0) break;
        printf("%s\n", name);
        any = 1;
    }

    if (!any) {
        printf("ls: %s: not a directory, empty, or doesn't exist\n", path);
    }

    return 0;
}
