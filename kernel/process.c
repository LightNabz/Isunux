#include "process.h"
#include "kutil.h"
#include "pmm.h"
#include "vmm.h"
#include "task.h"
#include "devfs.h"
#include "pipe.h"
#include "syscall.h"

static process_t process_pool[MAX_PROCESSES];
static int process_count = 0;
static int next_pid = 1;

/* Anonymous mmap()s live in their own arena, well clear of both the
 * brk-heap (which grows up from just past the ELF's segments, starting
 * well under 1MiB in) and the fixed 4-page user stack at USER_STACK_TOP
 * = 0x600000 (exec.c). 1GiB leaves a huge, realistically-uncollidable
 * gap for brk() to grow into before ever reaching here -- there's no
 * actual collision detection between the two arenas, same "simple and
 * correct for realistic use, not exhaustively guarded" scope as
 * process_brk() itself. */
#define MMAP_ARENA_BASE 0x40000000ULL

process_t *process_alloc(int parent_pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_pool[i].pid == 0) {
            process_t *p = &process_pool[i];
            k_memset(p, 0, sizeof(*p));
            p->pid = next_pid++;
            p->parent_pid = parent_pid;
            process_count++;
            return p;
        }
    }
    return NULL;
}

void process_init(process_t *p, uint64_t pml4_phys, uint64_t heap_start) {
    p->pml4_phys = pml4_phys;
    p->heap_start = heap_start;
    p->heap_end = heap_start; /* empty heap until the first brk() growth */
    p->mmap_next = MMAP_ARENA_BASE;
    p->cwd[0] = '/';
    p->cwd[1] = '\0';

    for (int fd = 0; fd < 3; fd++) {
        p->fds[fd].node = devfs_console_vnode();
        p->fds[fd].offset = 0;
        p->fds[fd].used = 1;
    }
}

void process_clone_into(process_t *dst, process_t *src, uint64_t new_pml4_phys) {
    dst->pml4_phys = new_pml4_phys;
    dst->heap_start = src->heap_start;
    dst->heap_end = src->heap_end;
    dst->mmap_next = src->mmap_next;
    for (int i = 0; i < VFS_MAX_PATH; i++) dst->cwd[i] = src->cwd[i];
    for (int fd = 0; fd < MAX_FDS; fd++) {
        dst->fds[fd] = src->fds[fd]; /* struct copy -- by value, see process.h note */
        if (dst->fds[fd].used && dst->fds[fd].node->ops && dst->fds[fd].node->ops->dup) {
            dst->fds[fd].node->ops->dup(dst->fds[fd].node);
        }
    }
}

process_t *process_current(void) {
    task_t *t = task_current();
    return t ? t->proc : NULL;
}

process_t *process_find_by_pid(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_pool[i].pid == pid) return &process_pool[i];
    }
    return NULL;
}

void process_terminate(process_t *proc, struct task *task, int exit_code) {
    /* real Unix closes every fd on process exit too -- without this, a
     * child holding the write end of a pipe that just exits (rather
     * than explicitly close()ing first, which is the completely normal
     * case) would never trigger EOF for whoever's reading the other
     * end, and they'd block forever. */
    for (int fd = 0; fd < MAX_FDS; fd++) {
        process_close(proc, fd);
    }
    process_mark_zombie(proc, exit_code);
    task->state = TASK_TERMINATED;
}

void process_mark_zombie(process_t *p, int exit_code) {
    p->exit_code = exit_code;
    p->is_zombie = 1;

    /* wake a parent that's blocked in process_waitpid() below, waiting
     * on either this specific child or "any child" -- both cases block
     * on the same channel (the waiting process's own process_t*), so
     * one task_wake() covers both */
    process_t *parent = process_find_by_pid(p->parent_pid);
    if (parent) task_wake(parent);
}

int64_t process_waitpid(process_t *self, int target_pid, int *status_out) {
    for (;;) {
        int found_any_child = 0;

for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &process_pool[i];
        if (p->pid == 0) continue;
        if (p->parent_pid != self->pid) continue;
        if (target_pid != -1 && p->pid != target_pid) continue;

        found_any_child = 1;

        if (p->is_zombie) {
            int reaped_pid = p->pid;
            if (status_out) *status_out = p->exit_code;
            vmm_destroy_address_space(p->pml4_phys); /* last chance -- p->pml4_phys is gone after the memset below */
            k_memset(p, 0, sizeof(*p));
            process_count--;
            return reaped_pid;
            }
        }

        if (!found_any_child) return -1; /* no such child(ren) at all */

        task_block(self); /* woken by process_mark_zombie() the moment a matching child exits, instead of polling every scheduler turn */
    }
}

uint64_t process_brk(process_t *p, uint64_t new_brk) {
    if (new_brk == 0) return p->heap_end; /* query mode, standard brk() behavior */
    if (new_brk <= p->heap_end) return p->heap_end; /* no shrinking yet -- silent no-op */

    uint64_t old_top = (p->heap_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t new_top = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint64_t addr = old_top; addr < new_top; addr += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0) return p->heap_end; /* out of memory -- break stays where it was */
        k_memset((uint8_t *)(vmm_hhdm_offset() + phys), 0, PAGE_SIZE); /* pmm_alloc_page() gives no zeroing guarantee -- a recycled page can carry a previous owner's leftover contents (same reasoning as process_mmap()) */
        vmm_map_4k_in(p->pml4_phys, addr, phys, PTE_WRITE | PTE_USER);
    }

    p->heap_end = new_brk;
    return p->heap_end;
}

uint64_t process_mmap(process_t *p, uint64_t addr_hint, uint64_t length, int prot, int flags) {
    (void)addr_hint; /* always ignored -- no MAP_FIXED support, we always place the mapping ourselves */

    if (length == 0) return 0;
    if (!(flags & MAP_ANONYMOUS)) return 0; /* no file-backed mapping yet -- needs Tier 2's real filesystem */

    uint64_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t base = p->mmap_next;

    uint64_t pte_flags = PTE_USER;
    if (prot & PROT_WRITE) pte_flags |= PTE_WRITE; /* PROT_READ/PROT_EXEC aren't separately tracked -- same simple model process_brk() already uses, no NX bit in play anywhere in this kernel */

    for (uint64_t i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0) {
            /* ran out partway through -- unwind what THIS call already
             * mapped rather than leave a half-built region sitting in
             * the process's address space that munmap() was never told
             * about */
            for (uint64_t j = 0; j < i; j++) {
                uint64_t v = base + j * PAGE_SIZE;
                uint64_t *pte = vmm_get_pte(p->pml4_phys, v);
                if (pte && (*pte & PTE_PRESENT)) {
                    pmm_free_page(*pte & ~0xFFFULL);
                    *pte = 0;
                }
            }
            return 0;
        }
        k_memset((uint8_t *)(vmm_hhdm_offset() + phys), 0, PAGE_SIZE); /* real mmap() guarantees zeroed pages -- pmm_alloc_page() gives no such guarantee itself (freed pages aren't scrubbed, so a recycled page can carry a previous owner's leftover contents), so this has to be explicit here */
        vmm_map_4k_in(p->pml4_phys, base + i * PAGE_SIZE, phys, pte_flags);
    }

    p->mmap_next = base + pages * PAGE_SIZE;
    return base;
}

int process_munmap(process_t *p, uint64_t addr, uint64_t length) {
    if (length == 0) return 0;

    uint64_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t v = addr + i * PAGE_SIZE;
        uint64_t *pte = vmm_get_pte(p->pml4_phys, v);
        if (!pte || !(*pte & PTE_PRESENT)) continue; /* not mapped -- matches real munmap()'s "fine, just skip it" behavior */

        pmm_free_page(*pte & ~0xFFFULL);
        *pte = 0;
        vmm_invalidate_page(v);
    }
    return 0;
}

int process_open(process_t *p, const char *path) {
    vnode_t *node = vfs_resolve_path_cwd(p->cwd, path);
    if (!node) return -1;

    for (int fd = 3; fd < MAX_FDS; fd++) {
        if (!p->fds[fd].used) {
            p->fds[fd].node = node;
            p->fds[fd].offset = 0;
            p->fds[fd].used = 1;
            return fd;
        }
    }
    return -1; /* out of fd slots */
}

int process_chdir(process_t *p, const char *path) {
    char combined[VFS_MAX_PATH];
    vfs_combine_path(p->cwd, path, combined, sizeof(combined));

    vnode_t *node = vfs_resolve_path(combined);
    if (!node || node->type != VNODE_DIR) return -1;

    /* store the canonical path, not the raw combined string -- so cwd
     * is always a clean "/etc", never "/../home/../etc", no matter
     * how many ".."s the user typed to get there. */
    vfs_canonical_path(node, p->cwd, VFS_MAX_PATH);
    return 0;
}

long process_read(process_t *p, int fd, void *buf, uint64_t count) {
    if (fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    vnode_t *node = p->fds[fd].node;
    if (!node->ops || !node->ops->read) return -1;

    long n = node->ops->read(node, buf, count, p->fds[fd].offset);
    if (n > 0) p->fds[fd].offset += (uint64_t)n;
    return n;
}

long process_write(process_t *p, int fd, const void *buf, uint64_t count) {
    if (fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    vnode_t *node = p->fds[fd].node;
    if (!node->ops || !node->ops->write) return -1;

    long n = node->ops->write(node, buf, count, p->fds[fd].offset);
    if (n > 0) p->fds[fd].offset += (uint64_t)n;
    return n;
}

int process_close(process_t *p, int fd) {
    if (fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (p->fds[fd].node->ops && p->fds[fd].node->ops->close) {
        p->fds[fd].node->ops->close(p->fds[fd].node);
    }
    p->fds[fd].used = 0;
    p->fds[fd].node = NULL;
    p->fds[fd].offset = 0;
    return 0;
}

/* Makes newfd become another reference to whatever oldfd currently
 * points at -- closing (with the same close-hook semantics as
 * process_close) whatever newfd used to be first, exactly like real
 * dup2(). This is the one primitive that makes both I/O redirection
 * and pipes possible: neither a redirected file nor a pipe end can be
 * reopened by path onto fd 0/1/2 (redirected files could in principle,
 * but a pipe end has no path at all -- it's anonymous), so the shell
 * always opens/creates it at some other fd, dup2()s it onto 0/1, then
 * closes the original. */
int process_dup2(process_t *p, int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= MAX_FDS || !p->fds[oldfd].used) return -1;
    if (newfd < 0 || newfd >= MAX_FDS) return -1;
    if (oldfd == newfd) return newfd;

    if (p->fds[newfd].used) {
        process_close(p, newfd);
    }

    p->fds[newfd].node = p->fds[oldfd].node;
    p->fds[newfd].offset = 0; /* independent offset, same documented scope cut as fork */
    p->fds[newfd].used = 1;

    if (p->fds[newfd].node->ops && p->fds[newfd].node->ops->dup) {
        p->fds[newfd].node->ops->dup(p->fds[newfd].node);
    }
    return newfd;
}

/* Creates a pipe and installs its two ends into p's fd table.
 * fds_out[0] = read end, fds_out[1] = write end -- same convention as
 * real pipe(2). Returns 0 on success, -1 if out of pipe slots or out
 * of fd slots (in which case any fd it did manage to allocate is
 * closed again, so a failed pipe() never leaks one end). */
int process_pipe(process_t *p, int fds_out[2]) {
    vnode_t *read_end, *write_end;
    if (pipe_create(&read_end, &write_end) != 0) return -1;

    int read_fd = -1, write_fd = -1;
    for (int fd = 3; fd < MAX_FDS; fd++) {
        if (!p->fds[fd].used) { read_fd = fd; break; }
    }
    if (read_fd >= 0) {
        for (int fd = read_fd + 1; fd < MAX_FDS; fd++) {
            if (!p->fds[fd].used) { write_fd = fd; break; }
        }
    }
    if (read_fd < 0 || write_fd < 0) {
        /* out of fd slots -- release the one reference pipe_create()
         * handed us for each end so the pipe doesn't leak */
        if (read_end->ops->close) read_end->ops->close(read_end);
        if (write_end->ops->close) write_end->ops->close(write_end);
        return -1;
    }

    p->fds[read_fd].node = read_end;
    p->fds[read_fd].offset = 0;
    p->fds[read_fd].used = 1;

    p->fds[write_fd].node = write_end;
    p->fds[write_fd].offset = 0;
    p->fds[write_fd].used = 1;

    fds_out[0] = read_fd;
    fds_out[1] = write_fd;
    return 0;
}

/* mkdir/create/unlink all need the same shape of work: resolve cwd +
 * path down to the PARENT directory, then hand the bare basename to
 * that directory's own vnode_ops -- vnode_ops has no "create myself",
 * only "create a child of this directory". vfs_split_path() is what
 * separates "/a/b/c" into parent "/a/b" and basename "c". */
static vnode_t *resolve_parent_dir(process_t *p, const char *path, char *base_out, uint64_t base_out_size) {
    char combined[VFS_MAX_PATH];
    vfs_combine_path(p->cwd, path, combined, sizeof(combined));

    char dir_path[VFS_MAX_PATH];
    vfs_split_path(combined, dir_path, sizeof(dir_path), base_out, base_out_size);

    if (base_out[0] == '\0') return NULL; /* e.g. path was just "/" */

    vnode_t *dir = vfs_resolve_path(dir_path);
    if (!dir || dir->type != VNODE_DIR) return NULL;
    return dir;
}

int process_mkdir(process_t *p, const char *path) {
    char name[VFS_MAX_NAME];
    vnode_t *dir = resolve_parent_dir(p, path, name, sizeof(name));
    if (!dir || !dir->ops || !dir->ops->mkdir) return -1;
    return dir->ops->mkdir(dir, name);
}

int process_create(process_t *p, const char *path) {
    char name[VFS_MAX_NAME];
    vnode_t *dir = resolve_parent_dir(p, path, name, sizeof(name));
    if (!dir || !dir->ops || !dir->ops->create) return -1;
    return dir->ops->create(dir, name);
}

int process_unlink(process_t *p, const char *path) {
    char name[VFS_MAX_NAME];
    vnode_t *dir = resolve_parent_dir(p, path, name, sizeof(name));
    if (!dir || !dir->ops || !dir->ops->unlink) return -1;
    return dir->ops->unlink(dir, name);
}

int process_stat(process_t *p, const char *path, vfs_stat_t *out) {
    vnode_t *node = vfs_resolve_path_cwd(p->cwd, path);
    if (!node) return -1;

    out->type = (uint64_t)node->type;
    out->size = 0;
    if (node->ops && node->ops->stat) {
        node->ops->stat(node, &out->size);
    }
    return 0;
}
