#include "vfs.h"
#include "tmpfs.h"
#include "kutil.h"
#include "devfs.h"

static vnode_t *root_vnode;

/* every embedded ELF, one pair of symbols per program name, generated
 * by the Makefile's %_blob.asm rule (kernel/userprog/NAME_blob.asm) */
extern uint8_t user_hello_elf_start[], user_hello_elf_end[];
extern uint8_t user_sh_elf_start[], user_sh_elf_end[];
extern uint8_t user_echo_elf_start[], user_echo_elf_end[];
extern uint8_t user_cat_elf_start[], user_cat_elf_end[];
extern uint8_t user_ls_elf_start[], user_ls_elf_end[];

typedef struct {
    const char *name;
    uint8_t *start;
    uint8_t *end;
} embedded_binary_t;

static embedded_binary_t embedded_binaries[] = {
    { "sh",    user_sh_elf_start,    user_sh_elf_end },
    { "hello", user_hello_elf_start, user_hello_elf_end },
    { "echo",  user_echo_elf_start,  user_echo_elf_end },
    { "cat",   user_cat_elf_start,   user_cat_elf_end },
    { "ls",    user_ls_elf_start,    user_ls_elf_end },
};
#define EMBEDDED_BINARY_COUNT (sizeof(embedded_binaries) / sizeof(embedded_binaries[0]))

void vfs_init(void) {
    tmpfs_init();
    root_vnode = tmpfs_get_root();

    /* seed one demo file so there's something real for open/read to
     * find -- written through the same vnode ops interface any syscall
     * would use, no special-casing */
    tmpfs_node_t *hello = tmpfs_create_file((tmpfs_node_t *)root_vnode, "hello.txt");
    const char *content = "this file lives in tmpfs, mapped through the vfs.\n";
    hello->vnode.ops->write(&hello->vnode, content, k_strlen(content), 0);

    /* seed every real executable ISUNUX ships, exactly the way any of
     * them would be loaded by exec() -- same embedded bytes the kernel
     * shipped with, just reachable as real paths via genuine tmpfs
     * read/write, no special-casing any one of them. */
    tmpfs_node_t *bin_dir = tmpfs_create_dir((tmpfs_node_t *)root_vnode, "bin");
    for (unsigned i = 0; i < EMBEDDED_BINARY_COUNT; i++) {
        tmpfs_node_t *f = tmpfs_create_file(bin_dir, embedded_binaries[i].name);
        uint64_t size = (uint64_t)(embedded_binaries[i].end - embedded_binaries[i].start);
        f->vnode.ops->write(&f->vnode, embedded_binaries[i].start, size, 0);
    }

    /* minimal standard layout -- just the directories that are
     * actually useful yet. Nothing here is mounted or special (it's
     * all still one tmpfs); this is purely "create these dirs at
     * boot" the same way /bin is. More (/var, /lib, /proc, a real
     * /dev backed by a devfs, ...) can show up later as something
     * actually needs them. */
    tmpfs_create_dir((tmpfs_node_t *)root_vnode, "tmp");
    tmpfs_create_dir((tmpfs_node_t *)root_vnode, "etc");
    tmpfs_create_dir((tmpfs_node_t *)root_vnode, "home");

    /* /dev is still an ordinary tmpfs directory as far as the tree is
     * concerned (that's what makes "/dev" resolvable at all, and what
     * makes ".." out of it work via the same vnode->parent convention
     * every other directory uses) -- devfs_install() just swaps its
     * ops so that looking *inside* it serves devices instead of
     * tmpfs-backed files. */
    tmpfs_node_t *dev_dir = tmpfs_create_dir((tmpfs_node_t *)root_vnode, "dev");
    devfs_install(&dev_dir->vnode);
}

vnode_t *vfs_root(void) {
    return root_vnode;
}

vnode_t *vfs_resolve_path(const char *path) {
    if (path[0] != '/') return NULL; /* only absolute paths for now */

    vnode_t *current = root_vnode;
    const char *p = path + 1;
    if (*p == '\0') return current; /* path was just "/" */

    char component[VFS_MAX_NAME];

    while (*p) {
        uint64_t i = 0;
        while (*p && *p != '/' && i < VFS_MAX_NAME - 1) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        if (*p == '/') p++;

        if (component[0] == '\0') {
            continue; /* empty component, e.g. from "//" or a trailing "/" -- just skip it */
        }
        if (k_strcmp(component, ".") == 0) {
            continue; /* stay at current */
        }
        if (k_strcmp(component, "..") == 0) {
            current = current->parent; /* root's parent is itself, so this never goes NULL */
            continue;
        }

        if (!current->ops || !current->ops->lookup) return NULL;
        current = current->ops->lookup(current, component);
        if (!current) return NULL;
    }

    return current;
}

void vfs_combine_path(const char *cwd, const char *path, char *out, uint64_t out_size) {
    if (path[0] == '/' || out_size == 0) {
        /* already absolute -- just copy it through */
        uint64_t i = 0;
        for (; path[i] && i < out_size - 1; i++) out[i] = path[i];
        out[i] = '\0';
        return;
    }

    uint64_t i = 0;
    for (; cwd[i] && i < out_size - 2; i++) out[i] = cwd[i];
    if (i == 0 || out[i - 1] != '/') out[i++] = '/';

    uint64_t j = 0;
    while (path[j] && i < out_size - 1) out[i++] = path[j++];
    out[i] = '\0';
}

vnode_t *vfs_resolve_path_cwd(const char *cwd, const char *path) {
    char combined[VFS_MAX_PATH];
    vfs_combine_path(cwd, path, combined, sizeof(combined));
    return vfs_resolve_path(combined);
}

int vfs_readdir_path(const char *cwd, const char *path, int index, char *name_out, uint64_t name_out_size) {
    vnode_t *node = vfs_resolve_path_cwd(cwd, path);
    if (!node || node->type != VNODE_DIR || !node->ops || !node->ops->readdir) return -1;
    return node->ops->readdir(node, index, name_out, name_out_size);
}

void vfs_canonical_path(vnode_t *node, char *out, uint64_t out_size) {
    /* Walk up via ->parent, collecting each ancestor (but not the
     * root itself -- root's own name is "/" and gets printed as the
     * leading separator instead). VFS_MAX_PATH is small, and a path
     * can't have more components than it has bytes, so a fixed-size
     * stack sized off out_size is always enough. */
    vnode_t *stack[VFS_MAX_PATH];
    int depth = 0;

    vnode_t *cur = node;
    while (cur->parent != cur && depth < VFS_MAX_PATH) {
        stack[depth++] = cur;
        cur = cur->parent;
    }
    /* cur is now the root (root is its own parent) */

    uint64_t i = 0;
    if (out_size > 0) out[0] = '/';
    i = (out_size > 0) ? 1 : 0;

    for (int d = depth - 1; d >= 0; d--) {
        const char *name = stack[d]->name;
        uint64_t j = 0;
        while (name[j] && i + 1 < out_size) out[i++] = name[j++];
        if (d != 0 && i + 1 < out_size) out[i++] = '/';
    }

    if (i < out_size) out[i] = '\0';
    else if (out_size > 0) out[out_size - 1] = '\0';
}
