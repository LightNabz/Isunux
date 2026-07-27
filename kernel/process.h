#pragma once
#include <stdint.h>
#include "vfs.h"

#define MAX_FDS 16

typedef struct {
    vnode_t *node;
    uint64_t offset;
    int used;
} fd_entry_t;

typedef struct {
    uint64_t pml4_phys;
    fd_entry_t fds[MAX_FDS];
} process_t;

/* Sets up fd 0/1/2 (stdin/stdout/stderr) pointed at the console vnode. */
void process_init(process_t *p, uint64_t pml4_phys);

int process_open(process_t *p, const char *path);
long process_read(process_t *p, int fd, void *buf, uint64_t count);
long process_write(process_t *p, int fd, const void *buf, uint64_t count);
int process_close(process_t *p, int fd);

/* The process the syscall dispatcher currently routes fd operations
 * against. Single global for now -- see the README note on why this
 * milestone deliberately stops short of full scheduler integration. */
extern process_t *current_process;
