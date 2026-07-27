#include "mini_malloc.h"
#include "mini_libc.h"

typedef struct block_header {
    size_t size;               /* usable size, not counting this header */
    int free;
    struct block_header *next;
} block_header_t;

static block_header_t *heap_head = NULL; /* first block ever created */
static block_header_t *heap_tail = NULL; /* most recently created -- new blocks append here */

#define ALIGN8(x) (((x) + 7u) & ~(size_t)7u)

/* Grows the process's break by exactly enough for one new block, and
 * carves the header + payload out of that freshly-mapped memory. */
static block_header_t *request_block(size_t size) {
    long current_brk = sys_brk(0); /* query mode */
    if (current_brk < 0) return NULL;

    unsigned long needed_end = (unsigned long)current_brk + sizeof(block_header_t) + size;
    long new_brk = sys_brk(needed_end);

    if ((unsigned long)new_brk < needed_end) return NULL; /* kernel couldn't grow that far */

    block_header_t *blk = (block_header_t *)(void *)(unsigned long)current_brk;
    blk->size = size;
    blk->free = 0;
    blk->next = NULL;
    return blk;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN8(size);

    /* first-fit: walk the existing blocks before ever asking the
     * kernel for more memory -- this is the whole reason free() matters */
    for (block_header_t *cur = heap_head; cur; cur = cur->next) {
        if (cur->free && cur->size >= size) {
            cur->free = 0;
            return (void *)(cur + 1);
        }
    }

    block_header_t *blk = request_block(size);
    if (!blk) return NULL;

    if (!heap_head) heap_head = blk;
    else heap_tail->next = blk;
    heap_tail = blk;

    return (void *)(blk + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    block_header_t *blk = (block_header_t *)ptr - 1;
    blk->free = 1;
    /* no coalescing of adjacent free blocks yet -- fine for now, since
     * proving reuse of a same-size block doesn't need it */
}
