#include "mini_libc.h"
#include "mini_malloc.h"

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

    /* --- now prove the heap actually works --- */
    const char *heap_msg = "\n--- heap test ---\n";
    sys_write(1, heap_msg, strlen_(heap_msg));

    char *a = malloc(64);
    const char *tag = "block A, allocated fresh from brk()\n";
    for (unsigned long i = 0; tag[i]; i++) a[i] = tag[i];
    sys_write(1, a, strlen_(tag));

    char *b = malloc(64); /* a second, distinct block -- keeps a's slot occupied for now */
    (void)b;

    free(a);
    char *c = malloc(64); /* same size as the freed block -- should reuse it exactly */

    if (c == a) {
        const char *ok = "malloc correctly reused the freed block\n";
        sys_write(1, ok, strlen_(ok));
    } else {
        const char *bad = "malloc did NOT reuse the freed block -- bug!\n";
        sys_write(1, bad, strlen_(bad));
    }

    sys_exit(0);
}
