#pragma once
#include <stdint.h>

#define VFS_MAX_NAME 64
#define VFS_MAX_PATH 128

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
    /* returns 1 and writes the index'th child's name into name_out on
     * success, 0 once index runs past the last child (end of
     * directory), -1 on error (e.g. called on something that isn't a
     * directory at all). */
    int (*readdir)(struct vnode *dir, int index, char *name_out, uint64_t name_out_size);
} vnode_ops_t;

typedef struct vnode {
    vnode_type_t type;
    char name[VFS_MAX_NAME];
    vnode_ops_t *ops;
    struct vnode *parent; /* the containing directory's vnode. The root
                            * is its own parent (same as real Unix: cd'ing
                            * ".." from "/" just stays at "/"). Filled in
                            * by whichever filesystem creates the node --
                            * lets vfs_resolve_path() handle ".." generically,
                            * without asking any specific filesystem for it. */
} vnode_t;

void vfs_init(void);
vnode_t *vfs_root(void);

/* Absolute paths only ("/foo/bar"), walked one component at a time via
 * each directory's ops->lookup, with "." and ".." handled generically
 * (via vnode->parent) before ever calling lookup. Returns NULL if any
 * component along the way doesn't exist. */
vnode_t *vfs_resolve_path(const char *path);

/* Combines a cwd and a path into an absolute path string in out (just
 * string concatenation with a '/' inserted -- "." and ".." components
 * are left in the string as-is and resolved later, by vfs_resolve_path).
 * If path is already absolute, cwd is ignored and path is copied through
 * unchanged. */
void vfs_combine_path(const char *cwd, const char *path, char *out, uint64_t out_size);

/* Resolves path relative to cwd (or absolutely, if path starts with
 * '/') -- what every cwd-aware syscall (open, chdir, exec) should call
 * instead of vfs_resolve_path directly. */
vnode_t *vfs_resolve_path_cwd(const char *cwd, const char *path);

/* Directory listing by path + index, since we don't have real
 * directory file descriptors yet -- simplest thing that actually
 * works, matches the rest of this project's scope philosophy. */
int vfs_readdir_path(const char *cwd, const char *path, int index, char *name_out, uint64_t name_out_size);

/* Rebuilds the canonical absolute path for a vnode by walking up its
 * parent chain to the root and printing the names root-to-leaf. This
 * is what real Unix kernels effectively do on every chdir(): whatever
 * you typed ("..", ".", a relative path) gets resolved down to a
 * vnode, and the STORED cwd becomes that vnode's real, minimal path --
 * never the raw string you typed. Without this, a cwd built purely by
 * string-concatenating "cd" arguments (see vfs_combine_path) would
 * grow forever, e.g. "/../home/../etc", instead of just "/etc". */
void vfs_canonical_path(vnode_t *node, char *out, uint64_t out_size);
