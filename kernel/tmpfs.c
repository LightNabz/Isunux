#include "tmpfs.h"
#include "pmm.h"
#include "vmm.h"
#include "kutil.h"
#include "errno.h"

#define TMPFS_MAX_NODES 128
#define TMPFS_FILE_INITIAL_PAGES 1 /* starting size -- grows on demand now, see tmpfs_file_write */

static tmpfs_node_t node_pool[TMPFS_MAX_NODES];
static int node_count = 0;

static tmpfs_node_t *root;

static tmpfs_node_t *tmpfs_alloc_node(void) {
    if (node_count >= TMPFS_MAX_NODES) return NULL;
    tmpfs_node_t *n = &node_pool[node_count++];
    k_memset(n, 0, sizeof(*n));
    return n;
}

static void set_name(vnode_t *v, const char *name) {
    uint64_t i = 0;
    for (; name[i] && i < VFS_MAX_NAME - 1; i++) v->name[i] = name[i];
    v->name[i] = '\0';
}

static long tmpfs_file_read(vnode_t *node, void *buf, uint64_t count, uint64_t offset) {
    tmpfs_node_t *f = (tmpfs_node_t *)node;
    if (offset >= f->size) return 0; /* EOF */

    uint64_t avail = f->size - offset;
    uint64_t n = count < avail ? count : avail;

    uint8_t *dst = (uint8_t *)buf;
    for (uint64_t i = 0; i < n; i++) dst[i] = f->data[offset + i];
    return (long)n;
}

static long tmpfs_file_write(vnode_t *node, const void *buf, uint64_t count, uint64_t offset) {
    tmpfs_node_t *f = (tmpfs_node_t *)node;
    uint64_t needed = offset + count;

    if (needed > f->capacity) {
        uint64_t new_cap_pages = f->capacity / PAGE_SIZE;
        if (new_cap_pages == 0) new_cap_pages = 1;
        while (new_cap_pages * PAGE_SIZE < needed) new_cap_pages *= 2;

        uint64_t new_phys = pmm_alloc_pages(new_cap_pages);
        if (new_phys == 0) {
            /* genuinely out of memory -- write whatever still fits in
             * the existing capacity instead of failing the call
             * outright, same "do what you can" spirit as process_brk()
             * hitting its own out-of-memory case */
            if (offset > f->capacity) return -ENOMEM;
            uint64_t space = f->capacity - offset;
            uint64_t n = count < space ? count : space;
            const uint8_t *src = (const uint8_t *)buf;
            for (uint64_t i = 0; i < n; i++) f->data[offset + i] = src[i];
            if (offset + n > f->size) f->size = offset + n;
            return (long)n;
        }

        uint8_t *new_data = (uint8_t *)(vmm_hhdm_offset() + new_phys);
        k_memcpy(new_data, f->data, f->size);
        pmm_free_pages((uint64_t)f->data - vmm_hhdm_offset(), f->capacity / PAGE_SIZE);
        f->data = new_data;
        f->capacity = new_cap_pages * PAGE_SIZE;
    }

    const uint8_t *src = (const uint8_t *)buf;
    for (uint64_t i = 0; i < count; i++) f->data[offset + i] = src[i];

    if (offset + count > f->size) f->size = offset + count;
    return (long)count;
}

static vnode_t *tmpfs_dir_lookup(vnode_t *dir_vnode, const char *name) {
    tmpfs_node_t *dir = (tmpfs_node_t *)dir_vnode;
    for (tmpfs_node_t *child = dir->first_child; child; child = child->next_sibling) {
        if (k_strcmp(child->vnode.name, name) == 0) return &child->vnode;
    }
    return NULL;
}

static int tmpfs_dir_readdir(vnode_t *dir_vnode, int index, char *name_out, uint64_t name_out_size) {
    tmpfs_node_t *dir = (tmpfs_node_t *)dir_vnode;
    int i = 0;
    for (tmpfs_node_t *child = dir->first_child; child; child = child->next_sibling, i++) {
        if (i != index) continue;
        uint64_t j = 0;
        for (; child->vnode.name[j] && j < name_out_size - 1; j++) name_out[j] = child->vnode.name[j];
        name_out[j] = '\0';
        return 1;
    }
    return 0; /* index is past the last child */
}

static int tmpfs_dir_create(vnode_t *dir_vnode, const char *name) {
    tmpfs_node_t *dir = (tmpfs_node_t *)dir_vnode;
    if (tmpfs_dir_lookup(dir_vnode, name)) return -EEXIST;
    return tmpfs_create_file(dir, name) ? 0 : -ENOMEM;
}

static int tmpfs_dir_mkdir(vnode_t *dir_vnode, const char *name) {
    tmpfs_node_t *dir = (tmpfs_node_t *)dir_vnode;
    if (tmpfs_dir_lookup(dir_vnode, name)) return -EEXIST;
    return tmpfs_create_dir(dir, name) ? 0 : -ENOMEM;
}

static int tmpfs_dir_unlink(vnode_t *dir_vnode, const char *name) {
    tmpfs_node_t *dir = (tmpfs_node_t *)dir_vnode;

    tmpfs_node_t *prev = NULL;
    tmpfs_node_t *child = dir->first_child;
    while (child && k_strcmp(child->vnode.name, name) != 0) {
        prev = child;
        child = child->next_sibling;
    }
    if (!child) return -ENOENT;

    if (child->vnode.type == VNODE_DIR && child->first_child != NULL) {
        return -ENOTEMPTY;
    }

    /* unlink from the sibling chain */
    if (prev) prev->next_sibling = child->next_sibling;
    else dir->first_child = child->next_sibling;

    /* reclaim the file's backing pages -- the tmpfs_node_t struct
     * itself is NOT returned to the pool (tmpfs has no free-list for
     * node slots, just a bump allocator); a documented scope cut for
     * how small and short-lived this filesystem needs to be for now. */
    if (child->vnode.type == VNODE_FILE && child->data != NULL) {
        uint64_t hhdm = vmm_hhdm_offset();
        uint64_t phys_base = (uint64_t)child->data - hhdm;
        for (uint64_t off = 0; off < child->capacity; off += PAGE_SIZE) {
            pmm_free_page(phys_base + off);
        }
    }

    return 0;
}

static int tmpfs_file_stat(vnode_t *node, uint64_t *size_out) {
    tmpfs_node_t *f = (tmpfs_node_t *)node;
    *size_out = f->size;
    return 0;
}

static vnode_ops_t tmpfs_file_ops = {
    .read = tmpfs_file_read,
    .write = tmpfs_file_write,
    .lookup = NULL,
    .readdir = NULL,
    .mkdir = NULL,
    .create = NULL,
    .unlink = NULL,
    .stat = tmpfs_file_stat,
};

static vnode_ops_t tmpfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .lookup = tmpfs_dir_lookup,
    .readdir = tmpfs_dir_readdir,
    .mkdir = tmpfs_dir_mkdir,
    .create = tmpfs_dir_create,
    .unlink = tmpfs_dir_unlink,
    .stat = NULL, /* directories report size 0 via the generic fallback, same as devfs */
};

void tmpfs_init(void) {
    root = tmpfs_alloc_node();
    root->vnode.type = VNODE_DIR;
    root->vnode.ops = &tmpfs_dir_ops;
    set_name(&root->vnode, "/");
    root->vnode.parent = &root->vnode; /* .. from / stays at / */
    root->vnode.mode = VFS_DEFAULT_DIR_MODE; /* root-owned (uid/gid 0, already zeroed) */
}

vnode_t *tmpfs_get_root(void) {
    return &root->vnode;
}

tmpfs_node_t *tmpfs_create_file(tmpfs_node_t *dir, const char *name) {
    tmpfs_node_t *f = tmpfs_alloc_node();
    if (!f) return NULL;

    f->vnode.type = VNODE_FILE;
    f->vnode.ops = &tmpfs_file_ops;
    set_name(&f->vnode, name);
    f->vnode.parent = &dir->vnode;
    f->vnode.mode = VFS_DEFAULT_FILE_MODE; /* uid/gid stay 0 (root) here -- the process-syscall
                                             * layer (process_create) overwrites them with the
                                             * creating process's real uid/gid right after this
                                             * returns; boot-time seeding (vfs_init) leaves files
                                             * root-owned, which is correct for /bin, /hello.txt, etc. */

    uint64_t phys = pmm_alloc_pages(TMPFS_FILE_INITIAL_PAGES);
    f->data = (uint8_t *)(vmm_hhdm_offset() + phys);
    f->capacity = TMPFS_FILE_INITIAL_PAGES * PAGE_SIZE;
    f->size = 0;

    f->next_sibling = dir->first_child;
    dir->first_child = f;
    return f;
}

tmpfs_node_t *tmpfs_create_dir(tmpfs_node_t *dir, const char *name) {
    tmpfs_node_t *d = tmpfs_alloc_node();
    if (!d) return NULL;

    d->vnode.type = VNODE_DIR;
    d->vnode.ops = &tmpfs_dir_ops;
    set_name(&d->vnode, name);
    d->vnode.parent = &dir->vnode;
    d->vnode.mode = VFS_DEFAULT_DIR_MODE; /* see tmpfs_create_file's note -- same deal */

    d->next_sibling = dir->first_child;
    dir->first_child = d;
    return d;
}
