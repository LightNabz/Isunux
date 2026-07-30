#include "pipe.h"
#include "task.h"
#include "kutil.h"

#define PIPE_BUF_SIZE 4096
#define MAX_PIPES 16

typedef struct pipe {
    uint8_t buf[PIPE_BUF_SIZE];
    uint64_t head;  /* next byte to read */
    uint64_t count; /* bytes currently buffered */

    int read_refcount;  /* how many open fds (across all processes) point at the read end */
    int write_refcount; /* same, for the write end */

    vnode_t read_vnode;
    vnode_t write_vnode;

    int in_use;
} pipe_t;

static pipe_t pipe_pool[MAX_PIPES];

static long pipe_read(vnode_t *node, void *buf, uint64_t count, uint64_t offset) {
    (void)offset; /* pipes aren't seekable */
    pipe_t *pipe = (pipe_t *)node->priv;
    uint8_t *dst = (uint8_t *)buf;

    while (pipe->count == 0) {
        if (pipe->write_refcount == 0) return 0; /* no writers left, buffer empty -- EOF */
        yield();
    }

    uint64_t n = 0;
    while (n < count && pipe->count > 0) {
        dst[n++] = pipe->buf[pipe->head];
        pipe->head = (pipe->head + 1) % PIPE_BUF_SIZE;
        pipe->count--;
    }
    return (long)n;
}

static long pipe_write(vnode_t *node, const void *buf, uint64_t count, uint64_t offset) {
    (void)offset;
    pipe_t *pipe = (pipe_t *)node->priv;
    const uint8_t *src = (const uint8_t *)buf;

    uint64_t n = 0;
    while (n < count) {
        if (pipe->read_refcount == 0) {
            /* broken pipe -- no SIGPIPE modeled, just report what we
             * actually got written (0 the first time through) */
            return n > 0 ? (long)n : -1;
        }
        if (pipe->count == PIPE_BUF_SIZE) {
            yield(); /* buffer full, wait for the reader to drain some */
            continue;
        }
        uint64_t tail = (pipe->head + pipe->count) % PIPE_BUF_SIZE;
        pipe->buf[tail] = src[n++];
        pipe->count++;
    }
    return (long)n;
}

static void pipe_read_dup(vnode_t *node) {
    ((pipe_t *)node->priv)->read_refcount++;
}
static void pipe_read_close(vnode_t *node) {
    pipe_t *pipe = (pipe_t *)node->priv;
    pipe->read_refcount--;
    if (pipe->read_refcount == 0 && pipe->write_refcount == 0) pipe->in_use = 0;
}
static void pipe_write_dup(vnode_t *node) {
    ((pipe_t *)node->priv)->write_refcount++;
}
static void pipe_write_close(vnode_t *node) {
    pipe_t *pipe = (pipe_t *)node->priv;
    pipe->write_refcount--;
    if (pipe->read_refcount == 0 && pipe->write_refcount == 0) pipe->in_use = 0;
}

static vnode_ops_t pipe_read_ops = {
    .read = pipe_read,
    .write = NULL,
    .lookup = NULL,
    .readdir = NULL,
    .mkdir = NULL,
    .create = NULL,
    .unlink = NULL,
    .stat = NULL,
    .dup = pipe_read_dup,
    .close = pipe_read_close,
};

static vnode_ops_t pipe_write_ops = {
    .read = NULL,
    .write = pipe_write,
    .lookup = NULL,
    .readdir = NULL,
    .mkdir = NULL,
    .create = NULL,
    .unlink = NULL,
    .stat = NULL,
    .dup = pipe_write_dup,
    .close = pipe_write_close,
};

int pipe_create(vnode_t **read_end_out, vnode_t **write_end_out) {
    pipe_t *pipe = NULL;
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipe_pool[i].in_use) { pipe = &pipe_pool[i]; break; }
    }
    if (!pipe) return -1;

    k_memset(pipe, 0, sizeof(*pipe));
    pipe->in_use = 1;
    pipe->read_refcount = 1;
    pipe->write_refcount = 1;

    pipe->read_vnode.type = VNODE_FILE;
    pipe->read_vnode.ops = &pipe_read_ops;
    pipe->read_vnode.parent = NULL;
    pipe->read_vnode.priv = pipe;

    pipe->write_vnode.type = VNODE_FILE;
    pipe->write_vnode.ops = &pipe_write_ops;
    pipe->write_vnode.parent = NULL;
    pipe->write_vnode.priv = pipe;

    *read_end_out = &pipe->read_vnode;
    *write_end_out = &pipe->write_vnode;
    return 0;
}
