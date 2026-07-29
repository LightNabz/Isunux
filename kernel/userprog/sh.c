#include "mini_libc.h"
#include "mini_string.h"
#include "mini_printf.h"
#include <stdint.h>

#define MAX_LINE 256
#define MAX_ARGS 16
#define MAX_PATH 128

static int split_line(char *line, char **argv_out) {
    int argc = 0;
    char *p = line;

    while (*p && argc < MAX_ARGS - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        argv_out[argc++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    argv_out[argc] = 0;
    return argc;
}

static void build_bin_path(char *out, uint64_t out_size, const char *cmd) {
    const char *prefix = "/bin/";
    uint64_t i = 0;
    for (; prefix[i] && i < out_size - 1; i++) out[i] = prefix[i];
    uint64_t j = 0;
    while (cmd[j] && i < out_size - 1) out[i++] = cmd[j++];
    out[i] = '\0';
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("\nISUNUX shell -- type a command (try: ls, cat hello.txt, hello, echo hi)\n");

    for (;;) {
        printf("$ ");

        char line[MAX_LINE];
        long n = sys_read(0, line, sizeof(line) - 1);
        if (n <= 0) continue;
        line[n] = '\0';

        char *cmd_argv[MAX_ARGS];
        int cmd_argc = split_line(line, cmd_argv);
        if (cmd_argc == 0) continue; /* just Enter on an empty line */

        /* cd and exit MUST be builtins -- a subprocess can never change
         * its parent's working directory or terminate its parent, so
         * these two can't be implemented as separate /bin/ programs no
         * matter how the rest of the shell is designed */
        if (strcmp(cmd_argv[0], "exit") == 0) {
            printf("bye!\n");
            sys_exit(0);
        }

        if (strcmp(cmd_argv[0], "cd") == 0) {
            const char *target = (cmd_argc > 1) ? cmd_argv[1] : "/";
            if (sys_chdir(target) != 0) {
                printf("cd: no such directory: %s\n", target);
            }
            continue;
        }

        char path[MAX_PATH];
        build_bin_path(path, sizeof(path), cmd_argv[0]);

        long pid = sys_fork();
        if (pid == 0) {
            sys_execve(path, cmd_argv);
            /* only reached if execve failed */
            printf("%s: command not found\n", cmd_argv[0]);
            sys_exit(127);
        } else if (pid > 0) {
            int status = 0;
            sys_waitpid((int)pid, &status);
        } else {
            printf("fork failed!\n");
        }
    }

    return 0;
}
