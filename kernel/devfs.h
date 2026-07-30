#pragma once
#include "vfs.h"

/* A minimal devfs. There's no real device-driver framework here --
 * just a small, fixed table of static device vnodes (tty, null, zero)
 * that lookup()/readdir() serve up by name. This is intentionally NOT
 * a separate mounted filesystem: /dev is an ordinary tmpfs directory
 * node whose vnode_ops get swapped out for devfs's own (see
 * devfs_install()), so it still lives in the normal tree and normal
 * path resolution ("/dev/tty") just works. */

/* Takes the vnode for an (already-created, empty) directory and turns
 * it into /dev: overwrites its ops with devfs's own lookup/readdir, so
 * looking inside it returns device vnodes instead of tmpfs children. */
void devfs_install(vnode_t *dev_dir_vnode);

/* The console device (name "tty" under /dev) -- exposed directly so
 * process_init() can wire fd 0/1/2 at it without needing a path
 * lookup this early in boot. */
vnode_t *devfs_console_vnode(void);
