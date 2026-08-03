#pragma once
#include <stdint.h>
#include "vfs.h"

#define MAX_FDS 16
#define MAX_PROCESSES 16

struct task; /* forward decl only -- avoids a circular include with task.h,
              * which needs the full process_t definition for task_t->proc */

typedef struct {
    vnode_t *node;
    uint64_t offset;
    int used;
} fd_entry_t;

typedef struct process {
    uint64_t pml4_phys;
    fd_entry_t fds[MAX_FDS];
    uint64_t heap_start; /* fixed once set -- just past the ELF's highest segment */
    uint64_t heap_end;   /* the current "break" -- grows via process_brk() */
    char cwd[VFS_MAX_PATH]; /* always absolute, no trailing slash except for "/" itself */
    int pid;
    int parent_pid;
    int exit_code;
    int is_zombie; /* set by sys_exit, cleared once a parent reaps it via waitpid */
    struct task *task;   /* the scheduler's thread of execution for this process */
} process_t;

/* Allocates a fresh process_t from the static pool with a real,
 * never-reused pid assigned. Everything else is zeroed -- callers
 * finish setup via process_init() or process_clone_into(). */
process_t *process_alloc(int parent_pid);

/* Sets up a freshly allocated process: address space, empty heap at
 * heap_start, and fd 0/1/2 (stdin/stdout/stderr) pointed at the console
 * vnode. Used for brand new processes (not fork()ed ones). */
void process_init(process_t *p, uint64_t pml4_phys, uint64_t heap_start);

/* fork()'s process-state half (the address-space half is
 * vmm_clone_lower_half, called separately): copies heap bounds and the
 * entire fd table by value from src into dst. Real POSIX fork() shares
 * open-file offsets between parent and child via a common "open file
 * description"; copying by value instead means the two fd tables
 * diverge independently after the fork -- a deliberate, documented
 * scope cut, not an oversight. */
void process_clone_into(process_t *dst, process_t *src, uint64_t new_pml4_phys);

int process_open(process_t *p, const char *path);
long process_read(process_t *p, int fd, void *buf, uint64_t count);
long process_write(process_t *p, int fd, const void *buf, uint64_t count);
int process_close(process_t *p, int fd);
int process_dup2(process_t *p, int oldfd, int newfd);
int process_pipe(process_t *p, int fds_out[2]);

int process_mkdir(process_t *p, const char *path);
int process_create(process_t *p, const char *path);
int process_unlink(process_t *p, const char *path);
int process_stat(process_t *p, const char *path, vfs_stat_t *out);

/* Resolves path relative to p's cwd (with full "." / ".." support --
 * see vfs_resolve_path), confirms it's a directory, and updates p->cwd
 * to the CANONICAL form of that resolved path (see vfs_canonical_path)
 * -- never the raw string the caller passed in. Returns 0 on success,
 * -1 if the path doesn't resolve or isn't a directory. */
int process_chdir(process_t *p, const char *path);

uint64_t process_brk(process_t *p, uint64_t new_brk);

/* Called by sys_exit -- records the exit code and marks this process a
 * zombie, so a parent's waitpid() can observe it. The address space
 * stays alive until a parent actually reaps it via waitpid() (below) --
 * a zombie's pml4_phys has to remain valid for exactly as long as the
 * exit code does, since both are only readable through the same
 * process_t slot. The task slot is reclaimed separately, whenever
 * task_alloc_raw() next needs to recycle it -- see task.c. */
void process_mark_zombie(process_t *p, int exit_code);

/* target_pid == -1 means "any child". Blocks via task_block() (a real
 * BLOCKED state, woken by process_mark_zombie() the moment a matching
 * child exits -- not a busy-yield poll loop) until a matching child
 * becomes a zombie, then reaps it -- tears down its address space
 * (vmm_destroy_address_space), clears its zombie flag, and returns its
 * pid, writing its exit code to *status_out if non-NULL. Returns -1
 * immediately if the calling process has no children matching
 * target_pid at all. */
int64_t process_waitpid(process_t *self, int target_pid, int *status_out);

/* Finds a process_t by pid, or NULL if no live process has that pid
 * right now (already reaped, or never existed). Exposed mainly for
 * sys_kill's target lookup. */
process_t *process_find_by_pid(int pid);

/* The actual "stop running, become a zombie" logic, shared by both a
 * process exiting itself (sys_exit) and one being force-terminated by
 * another process (sys_kill, for SIGKILL/SIGTERM/SIGINT -- none of
 * which can be caught or ignored, there being no sigaction() yet).
 * Closes every fd `proc` has open, marks it a zombie with exit_code
 * (process_mark_zombie), then marks `task` terminated. `task` doesn't
 * have to be the CALLER's own task -- sys_kill calls this against some
 * other, not-currently-running process's task directly. That's safe
 * specifically because this is a single-CPU cooperative scheduler: the
 * only task ever actually executing is the one that called this
 * function, so any OTHER task is guaranteed to be sitting idle (READY
 * or BLOCKED) right now, not mid-instruction somewhere unsafe to touch. */
void process_terminate(process_t *proc, struct task *task, int exit_code);

/* Resolves to task_current()'s associated process -- the process the
 * syscall dispatcher should route fd/heap/fork operations against.
 * Replaces the milestone-7 placeholder single global now that tasks are
 * properly scheduler-integrated. */
process_t *process_current(void);
