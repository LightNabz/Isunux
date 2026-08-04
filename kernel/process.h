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
    uint64_t mmap_next;  /* next unused address in the mmap arena -- see process_mmap() */
    char cwd[VFS_MAX_PATH]; /* always absolute, no trailing slash except for "/" itself */
    int pid;
    int parent_pid;
    int exit_code;
    int is_zombie; /* set by sys_exit, cleared once a parent reaps it via waitpid */
    int is_stopped;    /* job control (SIGTSTP) -- true while actually suspended; the process is still alive, NOT a zombie */
    int stop_signal;   /* which signal caused the current stop -- always SIGTSTP right now, but keeping it as a field rather than assuming keeps process_waitpid's reporting generic */
    int stop_reported; /* has some waiter already been told about THIS stop instance -- cleared again on the next stop, same one-shot-per-event idea process_mark_zombie's is_zombie already has for exits */
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

/* Anonymous-only mmap: allocates `length` worth of fresh, zeroed pages
 * (rounded up) from a dedicated arena separate from both the brk heap
 * and the stack (see MMAP_ARENA_BASE in process.c), maps them with the
 * requested PROT_* permissions, and returns the resulting address (or 0
 * on failure -- out of memory, zero length, or missing MAP_ANONYMOUS).
 * addr_hint is ignored -- no MAP_FIXED support, this always places the
 * mapping itself. The arena only ever grows; munmap doesn't return
 * space to it for reuse, same "grows but never reclaims its own
 * bookkeeping" scope cut process_brk() already has. */
uint64_t process_mmap(process_t *p, uint64_t addr_hint, uint64_t length, int prot, int flags);

/* Unmaps and frees `length` worth of pages (rounded up) starting at
 * addr. Silently skips any page in the range that isn't actually
 * mapped, matching real munmap()'s "unmapping something already
 * unmapped is fine" behavior. Returns 0 always in this scope-cut
 * version (no bad-address reporting yet). */
int process_munmap(process_t *p, uint64_t addr, uint64_t length);

/* Called by sys_exit -- records the exit code and marks this process a
 * zombie, so a parent's waitpid() can observe it. The address space
 * stays alive until a parent actually reaps it via waitpid() (below) --
 * a zombie's pml4_phys has to remain valid for exactly as long as the
 * exit code does, since both are only readable through the same
 * process_t slot. The task slot is reclaimed separately, whenever
 * task_alloc_raw() next needs to recycle it -- see task.c. */
void process_mark_zombie(process_t *p, int exit_code);

/* target_pid == -1 means "any child". Blocks via task_block() (a real
 * BLOCKED state, woken the moment a matching child's state changes --
 * not a busy-yield poll loop) until a matching child EITHER becomes a
 * zombie OR is newly stopped (SIGTSTP, job control), then reports it:
 *   - zombie: reaps it -- tears down its address space
 *     (vmm_destroy_address_space), clears its zombie flag, and returns
 *     its pid, with *status_out ENCODE_EXITED/ENCODE_SIGNALED-encoded.
 *   - newly stopped: does NOT reap it (it's still alive!) -- just
 *     returns its pid with *status_out ENCODE_STOPPED-encoded, and
 *     marks the stop as reported so a second waitpid() call doesn't
 *     report the exact same stop again (real waitpid() only reports
 *     each stop/continue event once too). A later re-stop (another
 *     SIGTSTP after a SIGCONT) is a new, separately-reportable event.
 * This kernel doesn't have a WUNTRACED-style opt-in -- stop-reporting
 * always happens, unconditionally, a reasonable simplification for a
 * single-shell system. Returns -1 immediately if the calling process
 * has no children matching target_pid at all. */
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

/* The actual per-signal delivery logic -- SIGKILL/SIGTERM/SIGINT
 * terminate `target` (via process_terminate, ENCODE_SIGNALED-encoded);
 * SIGTSTP suspends it (job control -- sets is_stopped, wakes a
 * waitpid()-ing parent, moves its task to TASK_STOPPED); SIGCONT
 * resumes a stopped one; SIGCHLD is a no-op (its real default action --
 * the parent-wakeup half of its job already happens unconditionally via
 * process_mark_zombie's task_wake(), whether or not this signal itself
 * is ever sent). Shared by sys_kill (one target, from a syscall) and
 * process_signal_foreground (below, possibly several targets in one
 * shot, from a keyboard IRQ).
 *
 * Deliberately does NOT yield() on the caller's behalf if `target`
 * turns out to be the calling task itself -- instead returns 0 (no
 * special action needed), 1 (target was self and is now
 * TASK_TERMINATED -- caller must loop yield() forever, never returning
 * to userspace), or 2 (target was self and is now TASK_STOPPED --
 * caller must yield() exactly once). This split matters for
 * process_signal_foreground: it may need to signal several
 * targets in one keyboard event, and one of them happening to be the
 * currently-running task can't be allowed to yield() away mid-loop
 * before the REST of the foreground set has been signaled too. Returns
 * -1 for an unrecognized signal number. */
int process_send_signal(process_t *target, int sig);

/* Records which pids should receive SIGINT/SIGTSTP when Ctrl-C/Ctrl-Z
 * is pressed (see keyboard.c) -- this kernel's stand-in for a real
 * process-group-based controlling terminal, which doesn't exist. count
 * is clamped to a small fixed bound (mirrors the shell's own
 * MAX_STAGES); pids beyond that are silently dropped, same "bounded and
 * simple over exhaustively general" scope cut as everywhere else here.
 * The shell calls this with its current pipeline's pids before
 * waiting on it in the foreground, and with count 0 once it's back at
 * the prompt (so a stray Ctrl-C there has nothing to hit -- notably,
 * NOT the shell's own pid, since the shell has no way to override
 * SIGINT's default terminate action without real sigaction() support). */
void process_set_foreground(const int *pids, int count);

/* Delivers sig (SIGINT or SIGTSTP, in practice) to every pid currently
 * in the foreground set via process_send_signal, then yields on the
 * calling task's behalf if (and only after) that turned out to be
 * necessary -- see process_send_signal's doc comment for why the
 * deferral matters here specifically. Safe to call from IRQ context: a
 * yield() from inside an interrupt handler is already an established
 * pattern in this kernel (irq.c's timer preemption does exactly this). */
void process_signal_foreground(int sig);
