#include "mini_libc.h"

/* No crt0, no libc startup -- _start IS the entry point, straight from
 * the ELF header. That's fine: we don't need argc/argv/environ or
 * global constructors for a program this small. */
void _start(void) {
    const char *msg1 = "starting vfs test (real compiled c, real elf!): opening /hello.txt\n";
    sys_write(1, msg1, strlen_(msg1));

    long fd = sys_open("/hello.txt");
    if (fd < 0) {
        const char *err = "open failed!\n";
        sys_write(2, err, strlen_(err));
        sys_exit(1);
    }

    char buf[128];
    long n = sys_read((int)fd, buf, sizeof(buf));

    sys_write(1, buf, (unsigned long)n);

    sys_close((int)fd);
    sys_exit(0);
}
