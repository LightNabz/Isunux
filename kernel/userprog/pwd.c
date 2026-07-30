#include "mini_libc.h"

int main(void) {
    char buf[256];
    long n = sys_getcwd(buf, sizeof(buf));
    if (n < 0) {
        const char *msg = "pwd: failed to get current directory\n";
        sys_write(2, msg, 38);
        return 1;
    }
    sys_write(1, buf, (unsigned long)n);
    sys_write(1, "\n", 1);
    return 0;
}
