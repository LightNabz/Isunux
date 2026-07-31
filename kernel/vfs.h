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
    /* Directory-mutation ops -- all optional (NULL is a legal value,
     * meaning "this filesystem doesn't support that", e.g. devfs's
     * fixed device table can't have new devices mkdir'd into it).
     * Return 0 on success, -1 on any failure (already exists, name too
     * long, out of nodes, not a directory, op unsupported, ...). */
    int (*mkdir)(struct vnode *dir, const char *name);
    int (*create)(struct vnode *dir, const char *name);
    /* Removes a child by name. Works on both files and EMPTY
     * directories -- real POSIX splits this into unlink() (files
     * only) and rmdir() (empty dirs only); one combined op is a
     * documented scope cut for this minimal filesystem. */
    int (*unlink)(struct vnode *dir, const char *name);
    /* Fills out->size (out->type is always set by the generic caller
     * from node->type, no filesystem needs to repeat that). NULL is
     * legal -- means "no notion of size" (e.g. devfs's tty/null/zero
     * are streams, not byte-addressed files), and callers should just
     * treat that as size 0. */
    int (*stat)(struct vnode *node, uint64_t *size_out);
    /* Both optional (NULL is the common case -- tmpfs/devfs nodes have
     * no per-reference lifecycle at all). Only something like a pipe,
     * where two ends share a single buffer and need to know exactly
     * how many open file descriptors reference each end, needs these:
     *   dup()   -- called once for every NEW fd that ends up pointing
     *              at this node (process_clone_into on fork, and
     *              process_dup2), i.e. "another reference exists now".
     *   close() -- called once for every fd referencing this node that
     *              gets closed (process_close, process_dup2 overwriting
     *              an fd, and sys_exit closing everything on the way
     *              out), i.e. "one fewer reference exists now". */
    void (*dup)(struct vnode *node);
    void (*close)(struct vnode *node);
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
    void *priv; /* owner-defined context. tmpfs/devfs don't need this --
                  * they embed vnode_t as the first member of a bigger
                  * struct and cast back (e.g. tmpfs_node_t). That trick
                  * doesn't work when TWO distinct vnodes need to reach
                  * the SAME shared object (a pipe's read end and write
                  * end both need to find the same pipe_t), which is
                  * what this field is for instead. */
} vnode_t;

struct limine_module_response; /* from limine.h -- forward-declared here
                                 * so vfs.h doesn't need to depend on the
                                 * boot-protocol header just for one
                                 * pointer type */

/* Sets up tmpfs, seeds the standard directory layout, and populates
 * /bin entirely from Limine boot modules (see kernel.c's
 * limine_module_request and boot/limine.conf's generated module_path
 * lines -- one per program under bin/, written by the Makefile). This
 * function has zero knowledge of what programs exist: adding, removing,
 * or updating a userland program never touches this file, or
 * kernel.elf at all. modules may be NULL (bootloader gave us none, or
 * an older Limine that doesn't support the request) -- /bin just ends
 * up empty in that case, everything else still boots fine. */
void vfs_init(struct limine_module_response *modules);
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

/* Splits "/some/dir/name" into dir_out="/some/dir" and base_out="name"
 * (or dir_out="/" and base_out="name" for a bare "/name"). Used by
 * mkdir/create/unlink, which all need "resolve the PARENT directory,
 * then operate on it by the child's bare name" -- vnode_ops has no
 * notion of "create myself", only "create a child of this directory". */
void vfs_split_path(const char *path, char *dir_out, uint64_t dir_out_size,
                     char *base_out, uint64_t base_out_size);

/* Minimal stat info. Kept intentionally tiny (no permissions, no
 * timestamps, no inode number -- none of that exists yet). type is a
 * vnode_type_t value (VNODE_FILE=0, VNODE_DIR=1). This layout is part
 * of the kernel/userland ABI: mini_libc.h's stat_t must stay in sync
 * with this by hand, same convention already used for the SYS_* syscall
 * numbers duplicated between syscall.h and mini_libc.h. */
typedef struct {
    uint64_t type;
    uint64_t size;
} vfs_stat_t;
