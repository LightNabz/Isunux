#include "mini_libc.h"
#include "mini_string.h"
#include "mini_printf.h"
#include <stdint.h>

#define MAX_LINE   256
#define MAX_ARGS   16
#define MAX_STAGES 8
#define MAX_PATH   128
#define MAX_TOKENS (MAX_ARGS * MAX_STAGES)
#define TOKEN_ARENA_SIZE 512

static void copy_bounded(char *dst, uint64_t dst_size, const char *src) {
    uint64_t i = 0;
    for (; src[i] && i + 1 < dst_size; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int is_var_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int str_to_int(const char *s) {
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

/* ==================== shell-local environment ====================
 * Real POSIX environment variables live in envp[], passed alongside
 * argv[] into every exec()'d process, and a child reads them itself
 * via getenv(). That kernel ABI is real now (do_exec() in exec.c) --
 * this table is still the shell's own bookkeeping (what $VAR expansion
 * and the PATH search below consult), but it's no longer a dead end:
 * main()'s envp parameter seeds it at startup (whatever THIS shell was
 * itself exec()'d with), and build_envp_array() below turns it back
 * into a real envp[] every time a child gets exec()'d, so changes this
 * shell makes (assignments, "export") actually do propagate to
 * children now, the same as a real shell. */
#define MAX_ENV 32
typedef struct {
    char name[32];
    char value[128];
    int used;
} env_var_t;

static env_var_t env_vars[MAX_ENV];

static const char *env_get(const char *name) {
    for (int i = 0; i < MAX_ENV; i++) {
        if (env_vars[i].used && strcmp(env_vars[i].name, name) == 0) return env_vars[i].value;
    }
    return NULL;
}

static void env_set(const char *name, const char *value) {
    for (int i = 0; i < MAX_ENV; i++) {
        if (env_vars[i].used && strcmp(env_vars[i].name, name) == 0) {
            copy_bounded(env_vars[i].value, sizeof(env_vars[i].value), value);
            return;
        }
    }
    for (int i = 0; i < MAX_ENV; i++) {
        if (!env_vars[i].used) {
            copy_bounded(env_vars[i].name, sizeof(env_vars[i].name), name);
            copy_bounded(env_vars[i].value, sizeof(env_vars[i].value), value);
            env_vars[i].used = 1;
            return;
        }
    }
    /* table full -- silently dropped, same spirit as every other
     * fixed-pool limit in this kernel */
}

static int is_assignment(const char *word) {
    if (!((word[0] >= 'a' && word[0] <= 'z') || (word[0] >= 'A' && word[0] <= 'Z') || word[0] == '_')) return 0;
    uint64_t i = 1;
    while (word[i] && is_var_char(word[i])) i++;
    return word[i] == '=';
}

static void do_assignment(const char *word) {
    char name[32];
    uint64_t i = 0;
    while (word[i] && word[i] != '=' && i < sizeof(name) - 1) { name[i] = word[i]; i++; }
    name[i] = '\0';
    env_set(name, word[i] == '=' ? &word[i + 1] : "");
}

/* Turns env_vars[] back into a real "NAME=VALUE" envp[] array to hand
 * to sys_execve(). Called right before an exec, always in the freshly
 * forked child (COW means writing these static buffers here never
 * touches the parent shell's copy) -- safe to keep as static storage
 * rather than allocating, same reasoning exec_argv_storage in the
 * kernel's exec.c already relies on. */
static char envp_strs[MAX_ENV][32 + 1 + 128]; /* name + '=' + value, bounds match env_var_t's own field sizes */
static char *envp_array[MAX_ENV + 1];

static char **build_envp_array(void) {
    int n = 0;
    for (int i = 0; i < MAX_ENV; i++) {
        if (!env_vars[i].used) continue;
        char *dst = envp_strs[n];
        int j = 0;
        for (int k = 0; env_vars[i].name[k]; k++) dst[j++] = env_vars[i].name[k];
        dst[j++] = '=';
        for (int k = 0; env_vars[i].value[k] && j < (int)sizeof(envp_strs[0]) - 1; k++) dst[j++] = env_vars[i].value[k];
        dst[j] = '\0';
        envp_array[n] = dst;
        n++;
    }
    envp_array[n] = 0;
    return envp_array;
}

/* ==================== job control ====================
 * Real job control tracks whole process GROUPS via a controlling
 * terminal (tcsetpgrp, SIGTTIN/SIGTTOU for background processes that
 * try to touch the terminal, etc.) -- none of that exists here. This is
 * a deliberately thin stand-in: the kernel's foreground pid SET
 * (sys_set_foreground, cleared back to empty once we're at the prompt)
 * is what Ctrl-C/Ctrl-Z actually target, and this table is purely the
 * SHELL's own memory of what it's told the kernel about, for jobs/fg/bg
 * to display and act on. The kernel doesn't know what a "job" is at
 * all -- only pids. */
#define MAX_JOBS 16
typedef struct {
    int used;
    int job_id;
    int pids[MAX_STAGES];
    int npids;
    int stopped; /* 1 = currently suspended (SIGTSTP), 0 = running (foreground wait returned, or backgrounded with '&') */
    char cmdline[MAX_LINE];
} job_t;

static job_t jobs[MAX_JOBS];
static int next_job_id = 1;

static int add_job(const int *pids, int npids, const char *cmdline, int stopped) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used) continue;
        jobs[i].used = 1;
        jobs[i].job_id = next_job_id++;
        jobs[i].npids = npids;
        for (int j = 0; j < npids; j++) jobs[i].pids[j] = pids[j];
        jobs[i].stopped = stopped;
        copy_bounded(jobs[i].cmdline, sizeof(jobs[i].cmdline), cmdline);
        return jobs[i].job_id;
    }
    return -1; /* table full -- the process itself still runs fine, we just lose the ability to fg/bg/list it by name */
}

static job_t *find_job_by_id(int id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used && jobs[i].job_id == id) return &jobs[i];
    }
    return NULL;
}

/* what bare "fg"/"bg" with no argument targets -- the most recently
 * added job, matching real shells' "current job" convention */
static job_t *find_most_recent_job(void) {
    job_t *best = NULL;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used && (!best || jobs[i].job_id > best->job_id)) best = &jobs[i];
    }
    return best;
}

/* accepts "%3", "3", or falls back to the most recent job if arg is NULL */
static job_t *resolve_job_arg(const char *arg) {
    if (!arg) return find_most_recent_job();
    return find_job_by_id(str_to_int(arg[0] == '%' ? arg + 1 : arg));
}

/* ==================== tokenizer ====================
 * Handles single/double quotes (spaces inside them don't split a
 * token; single quotes suppress $VAR expansion, double quotes still
 * allow it, same as real shells) and $VAR expansion. Operators (|, >,
 * <) split a token even with no surrounding whitespace ("ls|cat"
 * works, not just "ls | cat") as long as they're unquoted.
 * All word text is copied into a single static arena so tokens can
 * safely outlive the original input line (expansion can make a word
 * longer than what was originally typed, so tokens can't just be
 * pointers back into `line` the way a plain whitespace-split could). */
typedef enum { TOK_WORD, TOK_PIPE, TOK_REDIR_OUT, TOK_REDIR_IN, TOK_AMP } tok_type_t;
typedef struct { tok_type_t type; char *text; } token_t;

static char arena[TOKEN_ARENA_SIZE];
static uint64_t arena_pos;

static int tokenize(const char *line, token_t *toks, int max_toks) {
    arena_pos = 0;
    int n = 0;
    const char *p = line;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (n >= max_toks) break;

        if (*p == '|' || *p == '>' || *p == '<' || *p == '&') {
            toks[n].type = (*p == '|') ? TOK_PIPE : (*p == '>') ? TOK_REDIR_OUT : (*p == '<') ? TOK_REDIR_IN : TOK_AMP;
            toks[n].text = NULL;
            p++;
            n++;
            continue;
        }

        uint64_t start_pos = arena_pos;
        int in_single = 0, in_double = 0;

        while (*p) {
            if (!in_single && !in_double && (*p == ' ' || *p == '\t' || *p == '|' || *p == '>' || *p == '<' || *p == '&')) break;

            if (!in_double && *p == '\'') { in_single = !in_single; p++; continue; }
            if (!in_single && *p == '"') { in_double = !in_double; p++; continue; }

            if (!in_single && *p == '$') {
                p++;
                char varname[32];
                uint64_t vi = 0;
                while (*p && is_var_char(*p) && vi < sizeof(varname) - 1) { varname[vi++] = *p; p++; }
                varname[vi] = '\0';
                const char *val = env_get(varname);
                if (val) {
                    for (uint64_t k = 0; val[k] && arena_pos < TOKEN_ARENA_SIZE - 1; k++) arena[arena_pos++] = val[k];
                }
                continue; /* $NAME already fully consumed above */
            }

            if (arena_pos < TOKEN_ARENA_SIZE - 1) arena[arena_pos++] = *p;
            p++;
        }

        if (arena_pos < TOKEN_ARENA_SIZE) arena[arena_pos++] = '\0';
        toks[n].type = TOK_WORD;
        toks[n].text = &arena[start_pos];
        n++;
    }

    return n;
}

/* ==================== pipeline parsing ==================== */
typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    char *redirect_in;
    char *redirect_out;
} stage_t;

static int parse_pipeline(token_t *toks, int ntoks, stage_t *stages, int max_stages) {
    int nstages = 0;
    int i = 0;

    while (i < ntoks) {
        if (nstages >= max_stages) {
            printf("sh: too many pipeline stages\n");
            return -1;
        }
        stage_t *s = &stages[nstages];
        s->argc = 0;
        s->redirect_in = NULL;
        s->redirect_out = NULL;

        while (i < ntoks && toks[i].type != TOK_PIPE) {
            if (toks[i].type == TOK_REDIR_OUT || toks[i].type == TOK_REDIR_IN) {
                tok_type_t op = toks[i].type;
                i++;
                if (i >= ntoks || toks[i].type != TOK_WORD) {
                    printf("sh: syntax error near '%s'\n", op == TOK_REDIR_OUT ? ">" : "<");
                    return -1;
                }
                if (op == TOK_REDIR_OUT) s->redirect_out = toks[i].text;
                else s->redirect_in = toks[i].text;
                i++;
            } else {
                if (s->argc < MAX_ARGS - 1) s->argv[s->argc++] = toks[i].text;
                i++;
            }
        }
        s->argv[s->argc] = 0;
        nstages++;

        if (i < ntoks && toks[i].type == TOK_PIPE) {
            i++;
            if (i >= ntoks) {
                printf("sh: syntax error near '|'\n");
                return -1;
            }
        }
    }

    return nstages;
}

/* ==================== PATH search ====================
 * argv[0] containing a '/' (absolute or relative) bypasses PATH
 * entirely and is used as-is, same as real shells. Otherwise every
 * ':'-separated PATH entry is tried in order, confirmed with sys_stat
 * (so "command not found" is reported up front instead of only after
 * a failed execve). */
static int resolve_command(const char *cmd, char *out_path, uint64_t out_size) {
    for (uint64_t i = 0; cmd[i]; i++) {
        if (cmd[i] == '/') {
            copy_bounded(out_path, out_size, cmd);
            return 0;
        }
    }

    const char *path_env = env_get("PATH");
    if (!path_env) path_env = "/bin";

    const char *p = path_env;
    while (*p) {
        char dir[MAX_PATH];
        uint64_t di = 0;
        while (*p && *p != ':' && di < sizeof(dir) - 1) dir[di++] = *p++;
        dir[di] = '\0';
        if (*p == ':') p++;

        char candidate[MAX_PATH];
        uint64_t ci = 0;
        for (uint64_t k = 0; dir[k] && ci < sizeof(candidate) - 1; k++) candidate[ci++] = dir[k];
        if (ci == 0 || candidate[ci - 1] != '/') candidate[ci++] = '/';
        for (uint64_t k = 0; cmd[k] && ci < sizeof(candidate) - 1; k++) candidate[ci++] = cmd[k];
        candidate[ci] = '\0';

        stat_t st;
        if (sys_stat(candidate, &st) == 0 && st.type == VNODE_FILE_T) {
            copy_bounded(out_path, out_size, candidate);
            return 0;
        }
    }

    return -1;
}

/* Runs in the child, right after fork(), before execve(). ">" used to
 * fake O_TRUNC via sys_unlink()+sys_create() by hand, back when there
 * was no real O_TRUNC to ask for -- 1f gave process_open() a real
 * O_CREAT|O_TRUNC, so this is now exactly what every other shell's `>`
 * redirect does under the hood too, not a workaround anymore. */
static int setup_redirections(stage_t *s) {
    if (s->redirect_out) {
        long fd = sys_open3(s->redirect_out, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {
            printf("sh: cannot open %s for writing\n", s->redirect_out);
            return -1;
        }
        sys_dup2((int)fd, 1);
        sys_close((int)fd);
    }
    if (s->redirect_in) {
        long fd = sys_open(s->redirect_in);
        if (fd < 0) {
            printf("sh: cannot open %s\n", s->redirect_in);
            return -1;
        }
        sys_dup2((int)fd, 0);
        sys_close((int)fd);
    }
    return 0;
}

/* ==================== builtins ====================
 * cd/exit MUST be builtins -- a subprocess can never change its
 * parent's working directory or terminate its parent. export/env
 * could in principle be real /bin programs, but they need direct
 * access to the shell's own env table, so they stay builtins too.
 * None of these run inside a pipeline (nstages > 1) -- piping a
 * builtin isn't supported, a documented scope cut. */
/* Reclaims the foreground for the shell itself after a child (or
 * pipeline, or job) exits or stops. Always resets the console back to
 * canonical mode with echo on, alongside clearing the foreground pid
 * set -- a raw-mode or echo-off program that crashes or gets killed
 * must never leave the shell stuck reading unechoed, unbuffered
 * input. This is the one place that invariant is enforced; every
 * "back at the prompt" call site below goes through here instead of
 * calling sys_set_foreground(0, 0) directly, so it can't be
 * forgotten if a third call site is ever added. */
static void reclaim_foreground(void) {
    sys_set_foreground(0, 0);
    sys_tty_set_raw(0);
    sys_tty_set_echo(1);
}

static int run_builtin(stage_t *s) {
    if (s->argc == 0) return 1; /* nothing to do, but "handled" */

    if (strcmp(s->argv[0], "exit") == 0) {
        int code = (s->argc > 1) ? str_to_int(s->argv[1]) : 0;
        printf("bye!\n");
        sys_exit(code);
    }

    if (strcmp(s->argv[0], "cd") == 0) {
        const char *target = (s->argc > 1) ? s->argv[1] : env_get("HOME");
        if (!target) target = "/";
        if (sys_chdir(target) != 0) {
            printf("cd: no such directory: %s\n", target);
        }
        return 1;
    }

    if (strcmp(s->argv[0], "export") == 0) {
        for (int i = 1; i < s->argc; i++) {
            if (is_assignment(s->argv[i])) do_assignment(s->argv[i]);
            /* bare "export NAME" with no '=' is a no-op here -- there's
             * no envp mechanism for it to mark anything as exported */
        }
        return 1;
    }

    if (strcmp(s->argv[0], "env") == 0) {
        for (int i = 0; i < MAX_ENV; i++) {
            if (env_vars[i].used) printf("%s=%s\n", env_vars[i].name, env_vars[i].value);
        }
        return 1;
    }

    if (strcmp(s->argv[0], "kill") == 0) {
        /* %N job-spec support genuinely can't live in the standalone
         * bin/kill program -- that's a separate process with its own
         * address space, no visibility at all into THIS shell's jobs[]
         * table. Only a builtin can resolve %N, so this one shadows the
         * external binary entirely, same as real bash. */
        if (s->argc < 2) { printf("usage: kill [-SIG] <pid|%%job>\n"); return 1; }

        int sig = SIGTERM;
        int arg_i = 1;
        if (s->argv[1][0] == '-') {
            sig = str_to_int(s->argv[1] + 1);
            arg_i = 2;
            if (s->argc < 3) { printf("usage: kill [-SIG] <pid|%%job>\n"); return 1; }
        }

        const char *target = s->argv[arg_i];
        if (target[0] == '%') {
            job_t *j = find_job_by_id(str_to_int(target + 1));
            if (!j) { printf("kill: no such job\n"); return 1; }
            for (int k = 0; k < j->npids; k++) sys_kill(j->pids[k], sig);
            return 1;
        }

        int pid = str_to_int(target);
        if (pid <= 0) { printf("kill: invalid pid '%s'\n", target); return 1; }
        if (sys_kill(pid, sig) < 0) printf("kill: (%d) - no such process or unrecognized signal\n", pid);
        return 1;
    }

    if (strcmp(s->argv[0], "jobs") == 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (!jobs[i].used) continue;
            printf("[%d]%s %s\n", jobs[i].job_id, jobs[i].stopped ? "  Stopped" : "  Running", jobs[i].cmdline);
        }
        return 1;
    }

    if (strcmp(s->argv[0], "fg") == 0) {
        job_t *j = resolve_job_arg(s->argc > 1 ? s->argv[1] : NULL);
        if (!j) { printf("fg: no such job\n"); return 1; }


        printf("%s\n", j->cmdline);
        if (j->stopped) {
            for (int k = 0; k < j->npids; k++) sys_kill(j->pids[k], SIGCONT);
            j->stopped = 0;
        }

        sys_set_foreground(j->pids, j->npids);
        int any_stopped = 0;
        for (int k = 0; k < j->npids; k++) {
            int status = 0;
            sys_waitpid(j->pids[k], &status);
            if (WIFSTOPPED(status)) any_stopped = 1;
        }
        reclaim_foreground(); /* back at the prompt -- clear the foreground pid set and reset canonical/echo */

        if (any_stopped) {
            j->stopped = 1; /* re-stopped (another Ctrl-Z) -- keep the same job entry */
            printf("[%d]+  Stopped %s\n", j->job_id, j->cmdline);
        } else {
            j->used = 0; /* ran to completion -- done with this job */
        }
        return 1;
    }

    if (strcmp(s->argv[0], "bg") == 0) {
        job_t *j = resolve_job_arg(s->argc > 1 ? s->argv[1] : NULL);
        if (!j) { printf("bg: no such job\n"); return 1; }
        if (!j->stopped) { printf("bg: job already running\n"); return 1; }

        for (int k = 0; k < j->npids; k++) sys_kill(j->pids[k], SIGCONT);
        j->stopped = 0;
        printf("[%d]  %s &\n", j->job_id, j->cmdline);
        return 1;
    }

    return 0; /* not a builtin */
}

/* ==================== pipeline execution ==================== */
static void run_pipeline(stage_t *stages, int nstages, int background, const char *cmdline) {
    if (nstages == 1 && run_builtin(&stages[0])) return;
    if (nstages == 1 && stages[0].argc == 0) return;

    int pipe_fds[MAX_STAGES - 1][2];
    for (int i = 0; i < nstages - 1; i++) {
        if (sys_pipe(pipe_fds[i]) != 0) {
            printf("sh: pipe failed\n");
            return;
        }
    }

    int child_pids[MAX_STAGES];
    for (int i = 0; i < nstages; i++) {
        long pid = sys_fork();

        if (pid == 0) {
            if (i > 0) sys_dup2(pipe_fds[i - 1][0], 0);
            if (i < nstages - 1) sys_dup2(pipe_fds[i][1], 1);

            /* every pipe's BOTH ends get closed here, in every child --
             * including the ones we just dup2()'d, since dup2 leaves
             * the original fd open too. Skipping this is the classic
             * pipe deadlock: a stray extra reference to the write end
             * sitting in some unrelated process means the reader never
             * sees write_refcount hit zero, and blocks forever. */
            for (int j = 0; j < nstages - 1; j++) {
                sys_close(pipe_fds[j][0]);
                sys_close(pipe_fds[j][1]);
            }

            if (setup_redirections(&stages[i]) != 0) sys_exit(1);
            if (stages[i].argc == 0) sys_exit(0);

            char path[MAX_PATH];
            if (resolve_command(stages[i].argv[0], path, sizeof(path)) != 0) {
                printf("%s: command not found\n", stages[i].argv[0]);
                sys_exit(127);
            }

            sys_execve(path, stages[i].argv, build_envp_array());
            printf("%s: exec failed\n", stages[i].argv[0]);
            sys_exit(127);
        } else if (pid > 0) {
            child_pids[i] = (int)pid;
        } else {
            printf("sh: fork failed\n");
            child_pids[i] = -1;
        }
    }

    /* the shell's OWN copies of every pipe fd must close too, for
     * exactly the same reason as above -- otherwise the shell itself
     * is the stray reference that keeps a pipe end alive forever. */
    for (int j = 0; j < nstages - 1; j++) {
        sys_close(pipe_fds[j][0]);
        sys_close(pipe_fds[j][1]);
    }

    if (background) {
        int job_id = add_job(child_pids, nstages, cmdline, 0);
        printf("[%d]", job_id);
        for (int i = 0; i < nstages; i++) {
            if (child_pids[i] > 0) printf(" %d", child_pids[i]);
        }
        printf("\n");
        return;
    }

    /* foreground -- tell the kernel these pids should receive
     * Ctrl-C/Ctrl-Z, since it has no other notion of "which process is
     * in front" (see sys_set_foreground's doc comment in mini_libc.h) */
    sys_set_foreground(child_pids, nstages);

    int any_stopped = 0;
    for (int i = 0; i < nstages; i++) {
        if (child_pids[i] <= 0) continue;
        int status = 0;
        sys_waitpid(child_pids[i], &status);
        if (WIFSTOPPED(status)) any_stopped = 1; /* not reaped -- still alive, just suspended. Every OTHER stage in this same pipeline was in the foreground set too, so a Ctrl-Z here stopped the whole pipeline together, not just this one stage -- none of them are stuck blocked on a now-frozen upstream/downstream neighbor. */
    }

    reclaim_foreground(); /* back at the prompt -- clear the foreground pid set and reset canonical/echo, so a stray Ctrl-C/Ctrl-Z has nothing to hit (notably not even the shell's own pid -- see sys_set_foreground's doc comment for why) and a raw-mode/echo-off program that crashed doesn't leave the shell stuck */

    if (any_stopped) {
        int job_id = add_job(child_pids, nstages, cmdline, 1);
        printf("[%d]+  Stopped %s\n", job_id, cmdline);
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;

    env_set("PATH", "/bin");
    env_set("HOME", "/");

    /* seed from whatever this shell itself inherited, overriding the
     * bare-minimum defaults above wherever the parent actually
     * provided something -- same "a child sees what its exec()'ing
     * parent passed" model a real shell relies on (this is exactly how
     * su passes its own envp through to the shell it execs, and how
     * kernel.c seeds pid 1's very first environment). */
    if (envp) {
        for (int i = 0; envp[i]; i++) {
            const char *eq = envp[i];
            while (*eq && *eq != '=') eq++;
            if (*eq != '=') continue; /* malformed entry (no '=' at all) -- skip rather than guess */
            char name[32];
            uint64_t len = (uint64_t)(eq - envp[i]);
            if (len >= sizeof(name)) len = sizeof(name) - 1;
            for (uint64_t j = 0; j < len; j++) name[j] = envp[i][j];
            name[len] = '\0';
            env_set(name, eq + 1);
        }
    }

    printf("\nISUNUX shell -- type a command (try: ls, echo hi | cat, cat hello.txt > /tmp/copy.txt)\n");

    for (;;) {
        printf(sys_getuid() == 0 ? "# " : "$ ");

        char line[MAX_LINE];
        long n = sys_read(0, line, sizeof(line) - 1);
        if (n <= 0) continue;
        if (n > 0 && line[n - 1] == '\n') n--;
        line[n] = '\0';

        token_t toks[MAX_TOKENS];
        int ntoks = tokenize(line, toks, MAX_TOKENS);
        if (ntoks == 0) continue; /* just Enter on an empty line */

        int background = 0;
        if (toks[ntoks - 1].type == TOK_AMP) {
            background = 1;
            ntoks--;
            if (ntoks == 0) continue; /* a bare "&" with nothing before it -- nothing to run */
        }

        if (ntoks == 1 && toks[0].type == TOK_WORD && is_assignment(toks[0].text)) {
            do_assignment(toks[0].text);
            continue;
        }

        stage_t stages[MAX_STAGES];
        int nstages = parse_pipeline(toks, ntoks, stages, MAX_STAGES);
        if (nstages <= 0) continue;

        run_pipeline(stages, nstages, background, line);
    }

    return 0;
}
