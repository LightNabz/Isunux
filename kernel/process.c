#include "process.h"
#include "serial.h"
#include "kutil.h"
#include "pmm.h"
#include "vmm.h"
#include "task.h"
#include "keyboard.h"

static process_t process_pool[MAX_PROCESSES];
static int process_count = 0;
static int next_pid = 1;

/* The console isn't backed by tmpfs at all -- its read/write just call
 * straight into the serial driver. This is what lets SYS_WRITE stop
 * being "hardcoded to serial" and become genuinely "write to whatever
 * fd you were given": fd 1 just happens to point at a vnode whose write
 * op is the serial driver, and every other fd works through the exact
 * same process_write() path. */
static long console_write(vnode_t *node, const void *buf, uint64_t count, uint64_t offset) {
    (void)node;
    (void)offset;
    const char *s = (const char *)buf;
    for (uint64_t i = 0; i < count; i++) serial_putc(s[i]);
    return (long)count;
}

static long console_read(vnode_t *node, void *buf, uint64_t count, uint64_t offset) {
    (void)node;
    (void)offset;
    return keyboard_read(buf, count);
}

static vnode_ops_t console_ops = {
    .read = console_read,
    .write = console_write,
    .lookup = NULL,
};

static vnode_t console_vnode = {
    .type = VNODE_FILE,
    .name = "console",
    .ops = &console_ops,
};

process_t *process_alloc(int parent_pid) {
    if (process_count >= MAX_PROCESSES) return NULL;
    process_t *p = &process_pool[process_count++];
    k_memset(p, 0, sizeof(*p));
    p->pid = next_pid++;
    p->parent_pid = parent_pid;
    return p;
}

void process_init(process_t *p, uint64_t pml4_phys, uint64_t heap_start) {
    p->pml4_phys = pml4_phys;
    p->heap_start = heap_start;
    p->heap_end = heap_start; /* empty heap until the first brk() growth */
    p->cwd[0] = '/';
    p->cwd[1] = '\0';

    for (int fd = 0; fd < 3; fd++) {
        p->fds[fd].node = &console_vnode;
        p->fds[fd].offset = 0;
        p->fds[fd].used = 1;
    }
}

void process_clone_into(process_t *dst, process_t *src, uint64_t new_pml4_phys) {
    dst->pml4_phys = new_pml4_phys;
    dst->heap_start = src->heap_start;
    dst->heap_end = src->heap_end;
    for (int i = 0; i < VFS_MAX_PATH; i++) dst->cwd[i] = src->cwd[i];
    for (int fd = 0; fd < MAX_FDS; fd++) {
        dst->fds[fd] = src->fds[fd]; /* struct copy -- by value, see process.h note */
    }
}

process_t *process_current(void) {
    task_t *t = task_current();
    return t ? t->proc : NULL;
}

void process_mark_zombie(process_t *p, int exit_code) {
    p->exit_code = exit_code;
    p->is_zombie = 1;
}

int64_t process_waitpid(process_t *self, int target_pid, int *status_out) {
    for (;;) {
        int found_any_child = 0;

        for (int i = 0; i < process_count; i++) {
            process_t *p = &process_pool[i];
            if (p->parent_pid != self->pid) continue;
            if (target_pid != -1 && p->pid != target_pid) continue;

            found_any_child = 1;

            if (p->is_zombie) {
                if (status_out) *status_out = p->exit_code;
                p->is_zombie = 0; /* reaped -- resources still not freed, see process.h note */
                return p->pid;
            }
        }

        if (!found_any_child) return -1; /* no such child(ren) at all */

        yield(); /* block cooperatively until a matching child exits */
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
        vmm_map_4k_in(p->pml4_phys, addr, phys, PTE_WRITE | PTE_USER);
    }

    p->heap_end = new_brk;
    return p->heap_end;
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
    char new_cwd[VFS_MAX_PATH];
    vfs_combine_path(p->cwd, path, new_cwd, sizeof(new_cwd));

    vnode_t *node = vfs_resolve_path(new_cwd);
    if (!node || node->type != VNODE_DIR) return -1;

    for (int i = 0; i < VFS_MAX_PATH; i++) p->cwd[i] = new_cwd[i];
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
    p->fds[fd].used = 0;
    p->fds[fd].node = NULL;
    p->fds[fd].offset = 0;
    return 0;
}
