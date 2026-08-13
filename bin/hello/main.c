#include "mini_libc.h"
#include "mini_malloc.h"
#include "mini_string.h"
#include "mini_printf.h"

/* ISUNUX's first program with a genuine int main(int argc, char **argv)
 * -- crt0.asm reads argc/argv/envp straight off the SysV-layout stack
 * the kernel built and makes a real `call main`, exactly like a normal
 * C program anywhere else. No hand-wrapped _start in here anymore. */
int main(int argc, char **argv) {
    /* isolated interactive test -- kept separate from the normal flow
     * below since blocking on real keyboard input would hang the
     * automated (non-interactive) boot test forever */
    if (argc > 1 && strcmp(argv[1], "--kbdtest") == 0) {
        printf("type something and press enter:\n> ");
        char line[128];
        long n = sys_read(0, line, sizeof(line) - 1);
        line[n] = '\0';
        printf("got %d bytes back: %s", (int)n, line);
        return 0;
    }

    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d] = %s\n", i, argv[i]);
    }

    /* This same binary is also what exec() below loads -- if we're
     * running as that exec'd copy, skip straight past all the other
     * tests (which would otherwise fork/exec forever) and just prove
     * we got here as a genuinely fresh process image. */
    if (argc > 1 && strcmp(argv[1], "--no-recurse") == 0) {
        printf("\n[exec'd child] this is a FRESH process image, loaded via\n");
        printf("[exec'd child] execve(\"/bin/hello\", ...) through the real VFS.\n");
        printf("[exec'd child] exiting with code 42.\n");
        return 42;
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

    printf("\n--- readdir test ---\n");
    char name[64];
    for (int i = 0; ; i++) {
        long found = sys_readdir("/", i, name, sizeof(name));
        if (found <= 0) break;
        printf("  /%s\n", name);
    }

    printf("\n--- chdir + getcwd test ---\n");
    char cwd[128];
    sys_getcwd(cwd, sizeof(cwd));
    printf("cwd before: %s\n", cwd);

    if (sys_chdir("/bin") == 0) {
        sys_getcwd(cwd, sizeof(cwd));
        printf("cwd after chdir(\"/bin\"): %s\n", cwd);

        long relfd = sys_open("hello"); /* relative -- should resolve to /bin/hello */
        if (relfd >= 0) {
            printf("relative open(\"hello\") from /bin worked -- fd %d\n", (int)relfd);
            sys_close((int)relfd);
        } else {
            printf("relative open(\"hello\") FAILED -- bug!\n");
        }

        sys_chdir("/");
    } else {
        printf("chdir(\"/bin\") failed!\n");
    }

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

    printf("\n--- fork test ---\n");
    long pid = sys_fork();
    if (pid == 0) {
        printf("[child] hello from the child (fork returned 0 here)\n");
        for (int i = 0; i < 3; i++) {
            printf("[child] beat %d\n", i);
        }
        printf("[child] exiting\n");
        return 0;
    } else if (pid > 0) {
        printf("[parent] forked a child, got pid %d back\n", (int)pid);
        for (int i = 0; i < 3; i++) {
            printf("[parent] beat %d\n", i);
        }
        printf("[parent] exiting\n");
    } else {
        printf("fork failed!\n");
    }

    printf("\n--- fork + exec + waitpid test ---\n");
    long pid2 = sys_fork();
    if (pid2 == 0) {
        /* child: replace ourselves entirely with a fresh copy of this
         * same binary, loaded through the real VFS this time, with a
         * marker argv[1] so it doesn't recurse forever */
        char *new_argv[] = { "hello", "--no-recurse", 0 };
        sys_execve("/bin/hello", new_argv, 0);
        /* only reached if execve failed */
        printf("[child] execve failed!\n");
        sys_exit(1);
    } else if (pid2 > 0) {
        printf("[parent] forked pid %d, calling execve on it, now waiting...\n", (int)pid2);
        int status = 0;
        long reaped = sys_waitpid((int)pid2, &status);
        printf("[parent] reaped pid %d, exit code %d\n", (int)reaped, WEXITSTATUS(status));
    } else {
        printf("fork failed!\n");
    }

    return 0;
}
