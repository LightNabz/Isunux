#include "mini_libc.h"
#include "mini_string.h"

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        sys_write(1, argv[i], strlen(argv[i]));
        if (i < argc - 1) sys_write(1, " ", 1);
    }
    sys_write(1, "\n", 1);
    return 0;
}
