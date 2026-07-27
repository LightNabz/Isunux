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
    uint64_t heap_start; /* fixed once set -- just past the ELF's highest segment */
    uint64_t heap_end;   /* the current "break" -- grows via process_brk() */
} process_t;

/* Sets up fd 0/1/2 (stdin/stdout/stderr) pointed at the console vnode,
 * and starts the heap empty (heap_end == heap_start) at heap_start. */
void process_init(process_t *p, uint64_t pml4_phys, uint64_t heap_start);

int process_open(process_t *p, const char *path);
long process_read(process_t *p, int fd, void *buf, uint64_t count);
long process_write(process_t *p, int fd, const void *buf, uint64_t count);
int process_close(process_t *p, int fd);

/* Classic brk() semantics: new_brk == 0 queries the current break
 * without changing anything. Otherwise grows the break to new_brk,
 * mapping fresh pages as needed, and returns the (possibly unchanged)
 * break -- shrinking isn't supported yet, so a smaller new_brk is a
 * silent no-op, same as the pmm not supporting freeing back to the OS. */
uint64_t process_brk(process_t *p, uint64_t new_brk);

/* The process the syscall dispatcher currently routes fd/heap
 * operations against. Single global for now -- see the README note on
 * why this milestone deliberately stops short of full scheduler
 * integration. */
extern process_t *current_process;
