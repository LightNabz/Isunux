#include "tmpfs.h"
#include "pmm.h"
#include "vmm.h"
#include "kutil.h"

#define TMPFS_MAX_NODES 32
#define TMPFS_FILE_CAPACITY_PAGES 8 /* 32 KiB per file -- enough to hold a real compiled ELF for exec() */
#define TMPFS_FILE_CAPACITY (TMPFS_FILE_CAPACITY_PAGES * PAGE_SIZE)

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
    if (offset > f->capacity) return -1;

    uint64_t space = f->capacity - offset;
    uint64_t n = count < space ? count : space; /* silently truncates past 1 page -- fine for now */

    const uint8_t *src = (const uint8_t *)buf;
    for (uint64_t i = 0; i < n; i++) f->data[offset + i] = src[i];

    if (offset + n > f->size) f->size = offset + n;
    return (long)n;
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

static vnode_ops_t tmpfs_file_ops = {
    .read = tmpfs_file_read,
    .write = tmpfs_file_write,
    .lookup = NULL,
    .readdir = NULL,
};

static vnode_ops_t tmpfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .lookup = tmpfs_dir_lookup,
    .readdir = tmpfs_dir_readdir,
};

void tmpfs_init(void) {
    root = tmpfs_alloc_node();
    root->vnode.type = VNODE_DIR;
    root->vnode.ops = &tmpfs_dir_ops;
    set_name(&root->vnode, "/");
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

    uint64_t phys = pmm_alloc_pages(TMPFS_FILE_CAPACITY_PAGES);
    f->data = (uint8_t *)(vmm_hhdm_offset() + phys);
    f->capacity = TMPFS_FILE_CAPACITY;
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

    d->next_sibling = dir->first_child;
    dir->first_child = d;
    return d;
}
