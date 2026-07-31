#include "mini_libc.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    sys_write(1, "\f", 1);
    return 0;
}
