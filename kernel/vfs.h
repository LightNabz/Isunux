#pragma once
#include <stdint.h>

#define VFS_MAX_NAME 64

typedef enum {
    VNODE_FILE,
    VNODE_DIR,
} vnode_type_t;

struct vnode;

/* Every filesystem implementation (right now: just tmpfs) fills one of
 * these in and points its vnodes at it. The syscall layer only ever
 * talks to this table -- never to tmpfs directly -- so a real
 * disk-backed filesystem can be dropped in later without touching a
 * single syscall. */
typedef struct vnode_ops {
    long (*read)(struct vnode *node, void *buf, uint64_t count, uint64_t offset);
    long (*write)(struct vnode *node, const void *buf, uint64_t count, uint64_t offset);
    struct vnode *(*lookup)(struct vnode *dir, const char *name);
} vnode_ops_t;

typedef struct vnode {
    vnode_type_t type;
    char name[VFS_MAX_NAME];
    vnode_ops_t *ops;
} vnode_t;

void vfs_init(void);
vnode_t *vfs_root(void);

/* Absolute paths only for now ("/foo/bar"), walked one component at a
 * time via each directory's ops->lookup. Returns NULL if any component
 * along the way doesn't exist. */
vnode_t *vfs_resolve_path(const char *path);
