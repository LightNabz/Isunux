#include "vfs.h"
#include "tmpfs.h"
#include "kutil.h"
#include "devfs.h"
#include "limine.h"
#include "pmm.h"
#include "vmm.h"

static vnode_t *root_vnode;

static const char *basename_of(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    return base;
}

void vfs_init(struct limine_module_response *modules) {
    tmpfs_init();
    root_vnode = tmpfs_get_root();

    /* seed one demo file so there's something real for open/read to
     * find -- written through the same vnode ops interface any syscall
     * would use, no special-casing */
    tmpfs_node_t *hello = tmpfs_create_file((tmpfs_node_t *)root_vnode, "hello.txt");
    const char *content = "this file lives in tmpfs, mapped through the vfs.\n";
    hello->vnode.ops->write(&hello->vnode, content, k_strlen(content), 0);

    /* /bin is populated entirely from whatever boot modules Limine
     * handed us -- each module's basename becomes its filename under
     * /bin, bytes copied in through the exact same write() op any
     * syscall would use (so /bin stays an ordinary, mutable part of
     * tmpfs, not some special read-only region). This loop doesn't
     * know or care what any of these programs are named. */
    tmpfs_node_t *bin_dir = tmpfs_create_dir((tmpfs_node_t *)root_vnode, "bin");
    if (modules) {
        for (uint64_t i = 0; i < modules->module_count; i++) {
            struct limine_file *mod = modules->modules[i];
            tmpfs_node_t *f = tmpfs_create_file(bin_dir, basename_of(mod->path));
            f->vnode.ops->write(&f->vnode, mod->address, mod->size, 0);
            f->vnode.mode = VFS_DEFAULT_EXEC_MODE; /* 0755 -- these are programs, they need
                                                     * the execute bit or exec()'s new
                                                     * permission check would make every
                                                     * boot-seeded binary unrunnable. */
        }
    }

    /* minimal standard layout -- just the directories that are
     * actually useful yet. Nothing here is mounted or special (it's
     * all still one tmpfs); this is purely "create these dirs at
     * boot" the same way /bin is. More (/var, /lib, /proc, ...) can
     * show up later as something actually needs them. */
    tmpfs_create_dir((tmpfs_node_t *)root_vnode, "tmp");
    tmpfs_create_dir((tmpfs_node_t *)root_vnode, "etc");
    tmpfs_create_dir((tmpfs_node_t *)root_vnode, "home");
    tmpfs_create_dir((tmpfs_node_t *)root_vnode, "mnt"); /* FAT gets ops-swapped onto this in
                                                           * kernel.c, same trick devfs_install()
                                                           * uses for /dev -- see fat_install()'s
                                                           * doc comment in fat.h */

    /* /dev is still an ordinary tmpfs directory as far as the tree is
     * concerned (that's what makes "/dev" resolvable at all, and what
     * makes ".." out of it work via the same vnode->parent convention
     * every other directory uses) -- devfs_install() just swaps its
     * ops so that looking *inside* it serves devices instead of
     * tmpfs-backed files. */
    tmpfs_node_t *dev_dir = tmpfs_create_dir((tmpfs_node_t *)root_vnode, "dev");
    devfs_install(&dev_dir->vnode);
}

int vfs_check_perm(vnode_t *node, uint64_t uid, uint64_t gid, int want) {
    if (uid == 0) return 1; /* root -- bypasses all permission checks, same as real Unix */

    uint64_t bits;
    if (uid == node->uid) bits = (node->mode >> 6) & 0x7; /* owner bits */
    else if (gid == node->gid) bits = (node->mode >> 3) & 0x7; /* group bits */
    else bits = node->mode & 0x7; /* other bits */

    return (bits & (uint64_t)want) == (uint64_t)want;
}

void vfs_split_path(const char *path, char *dir_out, uint64_t dir_out_size,
                     char *base_out, uint64_t base_out_size) {
    /* find the last '/' -- everything before it is the parent dir,
     * everything after is the bare name being created/removed */
    int64_t last_slash = -1;
    for (int64_t i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash < 0) {
        /* no slash at all -- shouldn't happen for our always-absolute
         * paths, but handle it as "current dir" defensively */
        if (dir_out_size > 0) { dir_out[0] = '.'; dir_out[1] = '\0'; }
        uint64_t j = 0;
        for (; path[j] && j + 1 < base_out_size; j++) base_out[j] = path[j];
        base_out[j] = '\0';
        return;
    }

    if (last_slash == 0) {
        /* "/name" -- parent is just root */
        if (dir_out_size > 0) { dir_out[0] = '/'; dir_out[1] = '\0'; }
    } else {
        uint64_t n = (uint64_t)last_slash;
        if (n >= dir_out_size) n = dir_out_size - 1;
        for (uint64_t i = 0; i < n; i++) dir_out[i] = path[i];
        dir_out[n] = '\0';
    }

    const char *base = path + last_slash + 1;
    uint64_t j = 0;
    for (; base[j] && j + 1 < base_out_size; j++) base_out[j] = base[j];
    base_out[j] = '\0';
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

#define VFS_READ_INITIAL_PAGES 4 /* 16 KiB starting size -- covers every current binary in one shot, grows from there */

uint8_t *vfs_read_file_alloc(vnode_t *node, uint64_t *size_out, uint64_t *pages_out) {
    if (!node->ops || !node->ops->read) return NULL;

    uint64_t cap_pages = VFS_READ_INITIAL_PAGES;
    uint64_t phys = pmm_alloc_pages(cap_pages);
    if (phys == 0) return NULL;
    uint8_t *buf = (uint8_t *)(vmm_hhdm_offset() + phys);

    uint64_t total = 0;
    for (;;) {
        uint64_t cap_bytes = cap_pages * PAGE_SIZE;
        long n = node->ops->read(node, buf + total, cap_bytes - total, total);
        if (n < 0) {
            pmm_free_pages(phys, cap_pages);
            return NULL;
        }
        if (n == 0) break; /* real EOF -- read() only returns 0 once offset has run past the file's actual size */
        total += (uint64_t)n;

        if (total == cap_bytes) {
            /* buffer's completely full and the file might keep going --
             * double it rather than guess a bigger fixed size. Same
             * shape as pmm_alloc_pages()+copy+pmm_free_pages() used
             * elsewhere (e.g. vmm_clone_lower_half's per-page version),
             * just working in whole-buffer steps instead of per-page. */
            uint64_t new_cap_pages = cap_pages * 2;
            uint64_t new_phys = pmm_alloc_pages(new_cap_pages);
            if (new_phys == 0) {
                pmm_free_pages(phys, cap_pages);
                return NULL;
            }
            uint8_t *new_buf = (uint8_t *)(vmm_hhdm_offset() + new_phys);
            k_memcpy(new_buf, buf, total);
            pmm_free_pages(phys, cap_pages);
            phys = new_phys;
            buf = new_buf;
            cap_pages = new_cap_pages;
        }
    }

    if (size_out) *size_out = total;
    if (pages_out) *pages_out = cap_pages;
    return buf;
}

void vfs_read_file_free(uint8_t *buf, uint64_t pages) {
    if (!buf || pages == 0) return;
    uint64_t phys = (uint64_t)buf - vmm_hhdm_offset();
    pmm_free_pages(phys, pages);
}
