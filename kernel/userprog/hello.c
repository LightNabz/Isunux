#include "mini_libc.h"
#include "mini_malloc.h"
#include "mini_string.h"
#include "mini_printf.h"

/* ISUNUX's first program with a genuine int main(int argc, char **argv)
 * -- crt0.asm reads argc/argv/envp straight off the SysV-layout stack
 * the kernel built and makes a real `call main`, exactly like a normal
 * C program anywhere else. No hand-wrapped _start in here anymore. */
int main(int argc, char **argv) {
    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d] = %s\n", i, argv[i]);
    }

    printf("\n--- vfs test ---\n");
    long fd = sys_open("/hello.txt");
    if (fd < 0) {
        printf("open failed!\n");
        return 1;
    }

    char buf[128];
    long n = sys_read((int)fd, buf, sizeof(buf) - 1);
    buf[n] = '\0';
    printf("read %d bytes: %s", (int)n, buf);
    sys_close((int)fd);

    printf("\n--- heap test ---\n");
    char *a = malloc(64);
    const char *tag = "block A, allocated fresh from brk()\n";
    memcpy(a, tag, strlen(tag) + 1);
    printf("%s", a);

    char *b = malloc(64); /* keeps a's block occupied for now */
    (void)b;

    free(a);
    char *c = malloc(64); /* same size as the freed block -- should reuse it */

    if (c == a) {
        printf("malloc correctly reused the freed block\n");
    } else {
        printf("malloc did NOT reuse the freed block -- bug!\n");
    }

    return 0;
}
