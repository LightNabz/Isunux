#include "devfs.h"
#include "term.h"
#include "keyboard.h"
#include "kutil.h"

/* ---- /dev/tty -- the console. Reads pull from the keyboard driver,
 * writes go to the real user-visible terminal (screen + COM1). This
 * used to live directly in process.c as a one-off; moved here so it's
 * reachable by path ("/dev/tty") like a real device, not just wired
 * in directly at process-init time. ---- */
static long console_write(vnode_t *node, const void *buf, uint64_t count, uint64_t offset) {
    (void)node;
    (void)offset;
    const char *s = (const char *)buf;
    for (uint64_t i = 0; i < count; i++) term_putc(s[i]);
    return (long)count;
}

static long console_read(vnode_t *node, void *buf, uint64_t count, uint64_t offset) {
    (void)node;
    (void)offset;
    return keyboard_read(buf, count);
}

static vnode_ops_t console_ops = {
    .read = console_read,
    .write = console_write,
    .lookup = NULL,
    .readdir = NULL,
};

static vnode_t console_vnode = {
    .type = VNODE_FILE,
    .name = "tty",
    .ops = &console_ops,
    .parent = NULL, /* filled in by devfs_install() */
    .mode = 0666, /* world read+write, root-owned (uid/gid default to 0) --
                    * every process needs to reach the console regardless
                    * of who it's running as, same as real /dev/tty */
};

/* ---- /dev/null -- standard POSIX null-device semantics: reads
 * report EOF immediately, writes silently succeed and discard
 * everything. ---- */
static long null_read(vnode_t *node, void *buf, uint64_t count, uint64_t offset) {
    (void)node;
    (void)buf;
    (void)count;
    (void)offset;
    return 0; /* EOF, every time */
}

static long null_write(vnode_t *node, const void *buf, uint64_t count, uint64_t offset) {
    (void)node;
    (void)buf;
    (void)offset;
    return (long)count; /* pretend we wrote it all */
}

static vnode_ops_t null_ops = {
    .read = null_read,
    .write = null_write,
    .lookup = NULL,
    .readdir = NULL,
};

static vnode_t null_vnode = {
    .type = VNODE_FILE,
    .name = "null",
    .ops = &null_ops,
    .parent = NULL,
    .mode = 0666, /* world read+write, same reasoning as console_vnode above */
};

/* ---- /dev/zero -- reads always fill the buffer with zero bytes;
 * writes behave exactly like /dev/null. ---- */
static long zero_read(vnode_t *node, void *buf, uint64_t count, uint64_t offset) {
    (void)node;
    (void)offset;
    k_memset(buf, 0, count);
    return (long)count;
}

static vnode_ops_t zero_ops = {
    .read = zero_read,
    .write = null_write,
    .lookup = NULL,
    .readdir = NULL,
};

static vnode_t zero_vnode = {
    .type = VNODE_FILE,
    .name = "zero",
    .ops = &zero_ops,
    .parent = NULL,
    .mode = 0666, /* world read+write, same reasoning as console_vnode above */
};

typedef struct {
    const char *name;
    vnode_t *node;
} devfs_entry_t;

static devfs_entry_t devices[] = {
    { "tty",  &console_vnode },
    { "null", &null_vnode },
    { "zero", &zero_vnode },
};
#define DEVICE_COUNT (sizeof(devices) / sizeof(devices[0]))

static vnode_t *devfs_lookup(vnode_t *dir, const char *name) {
    (void)dir;
    for (unsigned i = 0; i < DEVICE_COUNT; i++) {
        if (k_strcmp(devices[i].name, name) == 0) return devices[i].node;
    }
    return NULL;
}

static int devfs_readdir(vnode_t *dir, int index, char *name_out, uint64_t name_out_size) {
    (void)dir;
    if (index < 0 || (unsigned)index >= DEVICE_COUNT) return 0;

    const char *name = devices[index].name;
    uint64_t i = 0;
    for (; name[i] && i < name_out_size - 1; i++) name_out[i] = name[i];
    name_out[i] = '\0';
    return 1;
}

static vnode_ops_t devfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .lookup = devfs_lookup,
    .readdir = devfs_readdir,
};

void devfs_install(vnode_t *dev_dir_vnode) {
    /* every device's parent is the /dev directory itself -- same
     * convention tmpfs uses for its own nodes, which is what lets
     * vfs_canonical_path() treat a path through /dev exactly like any
     * other directory, with no special-casing. */
    console_vnode.parent = dev_dir_vnode;
    null_vnode.parent = dev_dir_vnode;
    zero_vnode.parent = dev_dir_vnode;

    dev_dir_vnode->ops = &devfs_dir_ops;
}

vnode_t *devfs_console_vnode(void) {
    return &console_vnode;
}
