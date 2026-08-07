#include "mini_libc.h"
#include "mini_printf.h"

/* usage: chmod <octal-mode> <path> [path...]
 * Only a plain 3-digit octal mode is accepted (e.g. 755, 644) -- no
 * symbolic mode strings (u+x, go-w, ...), same "simplest thing that
 * actually works" scope cut as everywhere else in this project. */
int main(int argc, char **argv) {
    if (argc < 3) {
        const char *msg = "usage: chmod <octal-mode> <path>...\n";
        sys_write(2, msg, 37);
        return 1;
    }

    const char *p = argv[1];
    unsigned long mode = 0;
    int any_digit = 0;
    while (*p >= '0' && *p <= '7') { mode = mode * 8 + (unsigned long)(*p - '0'); p++; any_digit = 1; }
    if (!any_digit || *p != '\0') {
        printf("chmod: invalid mode '%s' (expected octal, e.g. 755)\n", argv[1]);
        return 1;
    }

    int status = 0;
    for (int i = 2; i < argc; i++) {
        if (sys_chmod(argv[i], mode) < 0) {
            printf("chmod: cannot change '%s'\n", argv[i]);
            status = 1;
        }
    }
    return status;
}
