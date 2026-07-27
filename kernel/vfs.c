#include "vfs.h"
#include "tmpfs.h"
#include "kutil.h"

static vnode_t *root_vnode;

void vfs_init(void) {
    tmpfs_init();
    root_vnode = tmpfs_get_root();

    /* seed one demo file so there's something real for open/read to
     * find -- written through the same vnode ops interface any syscall
     * would use, no special-casing */
    tmpfs_node_t *hello = tmpfs_create_file((tmpfs_node_t *)root_vnode, "hello.txt");
    const char *content = "this file lives in tmpfs, mapped through the vfs.\n";
    hello->vnode.ops->write(&hello->vnode, content, k_strlen(content), 0);
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

        if (!current->ops || !current->ops->lookup) return NULL;
        current = current->ops->lookup(current, component);
        if (!current) return NULL;
    }

    return current;
}
