#pragma once
#include "vfs.h"

typedef struct tmpfs_node {
    vnode_t vnode; /* MUST be first member -- lets us cast a vnode_t*
                    * straight to a tmpfs_node_t* and back, since they
                    * share the same starting address. Standard C
                    * "poor man's inheritance" trick. */
    uint8_t *data;              /* file content, backed by a PMM page via HHDM. NULL for dirs. */
    uint64_t size;               /* current content length (files only) */
    uint64_t capacity;            /* allocated buffer size (files only) */
    struct tmpfs_node *first_child; /* dirs only */
    struct tmpfs_node *next_sibling;
} tmpfs_node_t;

void tmpfs_init(void);
vnode_t *tmpfs_get_root(void);

tmpfs_node_t *tmpfs_create_file(tmpfs_node_t *dir, const char *name);
tmpfs_node_t *tmpfs_create_dir(tmpfs_node_t *dir, const char *name);
