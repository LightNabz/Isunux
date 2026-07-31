#include "mini_libc.h"
#include "mini_printf.h"
#include "mini_string.h"

#define LOGO_WIDTH 16

static const char *logo[] = {
    " .-----------.",
    " | > _       |",
    " |           |",
    " '-----------'",
};
#define LOGO_LINES ((int)(sizeof(logo) / sizeof(logo[0])))

static int count_entries(const char *path) {
    char name[64];
    int n = 0;
    for (int i = 0; ; i++) {
        long found = sys_readdir(path, i, name, sizeof(name));
        if (found <= 0) break;
        n++;
    }
    return n;
}

static void pad(int printed) {
    for (int i = printed; i < LOGO_WIDTH; i++) printf(" ");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char cwd[128];
    if (sys_getcwd(cwd, sizeof(cwd)) <= 0) {
        cwd[0] = '/';
        cwd[1] = '\0';
    }

    int bins = count_entries("/bin");
    int total_rows = LOGO_LINES > 6 ? LOGO_LINES : 6;

    for (int row = 0; row < total_rows; row++) {
        if (row < LOGO_LINES) {
            printf("%s", logo[row]);
            pad((int)strlen(logo[row]));
        } else {
            pad(0);
        }

        switch (row) {
            case 0: printf("root@isunux\n"); break;
            case 1: printf("-----------\n"); break;
            case 2: printf("OS: ISUNUX x86_64\n"); break;
            case 3: printf("Shell: /bin/sh\n"); break;
            case 4: printf("CWD: %s\n", cwd); break;
            case 5: printf("Bins: %d in /bin\n", bins); break;
            default: printf("\n"); break;
        }
    }

    return 0;
}