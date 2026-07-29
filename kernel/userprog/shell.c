#include "mini_libc.h"
#include "mini_string.h"
#include "mini_printf.h"

#define PROMPT "$ "
#define LINE_BUF_SIZE 256
#define MAX_ARGS 16
#define READ_BUF_SIZE 128

static void shell_prompt(void) {
    printf(PROMPT);
}

static void shell_print_error(const char *msg, const char *arg) {
    printf("%s", msg);
    if (arg) printf(" %s", arg);
    printf("\n");
}

static int split_args(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;

    while (*p && argc < max_args - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }

    argv[argc] = NULL;
    return argc;
}

static void shell_help(void) {
    printf("Available commands:\n");
    printf("  help\tShow this help\n");
    printf("  exit\tExit the shell\n");
    printf("  hello\tRun /bin/hello\n");
    printf("  exec <path>\tExecute a binary file\n");
    printf("  cat <path>\tPrint a file\n");
}

static void shell_cat(const char *path) {
    int fd = sys_open(path);
    if (fd < 0) {
        shell_print_error("cat: cannot open", path);
        return;
    }

    char buf[READ_BUF_SIZE];
    while (1) {
        long n = sys_read(fd, buf, READ_BUF_SIZE);
        if (n <= 0) break;
        sys_write(1, buf, (unsigned long)n);
    }
    sys_close(fd);
}

static void shell_exec_path(const char *path) {
    long ret = sys_exec(path);
    if (ret < 0) shell_print_error("exec failed", path);
}

static void shell_run_command(int argc, char *argv[]) {
    if (argc == 0) return;

    if (strcmp(argv[0], "help") == 0) {
        shell_help();
        return;
    }

    if (strcmp(argv[0], "exit") == 0) {
        sys_exit(0);
        return;
    }

    if (strcmp(argv[0], "hello") == 0) {
        shell_exec_path("/bin/hello");
        return;
    }

    if (strcmp(argv[0], "exec") == 0) {
        if (argc < 2) {
            shell_print_error("usage: exec", "exec <path>");
            return;
        }
        shell_exec_path(argv[1]);
        return;
    }

    if (strcmp(argv[0], "cat") == 0) {
        if (argc < 2) {
            shell_print_error("usage: cat", "cat <path>");
            return;
        }
        shell_cat(argv[1]);
        return;
    }

    /* Default behavior: if command contains a slash, try directly; else try /bin/<cmd>. */
    if (argv[0][0] == '/' || argv[0][0] == '.') {
        shell_exec_path(argv[0]);
        return;
    }

    char path[LINE_BUF_SIZE];
    int i = 0;
    const char *prefix = "/bin/";
    while (prefix[i]) { path[i] = prefix[i]; i++; }
    int j = 0;
    while (argv[0][j] && i < LINE_BUF_SIZE - 1) { path[i++] = argv[0][j++]; }
    path[i] = '\0';
    shell_exec_path(path);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char line[LINE_BUF_SIZE];
    char *argvp[MAX_ARGS];

    while (1) {
        shell_prompt();
        int pos = 0;

        while (pos < LINE_BUF_SIZE - 1) {
            char c;
            long n = sys_read(0, &c, 1);
            if (n <= 0) continue;

            if (c == '\r') c = '\n';
            if (c == '\n') {
                sys_write(1, "\n", 1);
                break;
            }
            if (c == '\b' || c == 0x7f) {
                if (pos > 0) {
                    pos--;
                    sys_write(1, "\b \b", 3);
                }
                continue;
            }
            if (c >= ' ' && c <= '~') {
                line[pos++] = c;
                sys_write(1, &c, 1);
            }
        }

        line[pos] = '\0';
        if (pos == 0) continue;

        int argc2 = split_args(line, argvp, MAX_ARGS);
        shell_run_command(argc2, argvp);
    }

    return 0;
}
