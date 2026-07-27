#include "process.h"
#include "serial.h"
#include "kutil.h"

process_t *current_process = NULL;

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
    (void)buf;
    (void)count;
    (void)offset;
    return 0; /* no real stdin yet -- behaves like immediate EOF */
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

void process_init(process_t *p, uint64_t pml4_phys) {
    k_memset(p, 0, sizeof(*p));
    p->pml4_phys = pml4_phys;

    for (int fd = 0; fd < 3; fd++) {
        p->fds[fd].node = &console_vnode;
        p->fds[fd].offset = 0;
        p->fds[fd].used = 1;
    }
}

int process_open(process_t *p, const char *path) {
    vnode_t *node = vfs_resolve_path(path);
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
