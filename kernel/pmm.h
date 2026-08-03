#pragma once
#include <stdint.h>
#include "limine.h"

#define PAGE_SIZE 4096ULL

/* How much physical memory the PMM bitmap (and therefore the VMM's HHDM
 * mapping) tracks. Kept as a shared define so the two stay in sync. */
#define PMM_TRACKED_PHYS_MEMORY (4ULL * 1024 * 1024 * 1024)

void pmm_init(struct limine_memmap_response *memmap);

/* Returns the physical address of a freshly allocated 4KiB page, or 0 if
 * we're out of memory. 0 is never a valid allocation because page 0 is
 * always marked reserved (it's the real-mode IVT/BDA). */
uint64_t pmm_alloc_page(void);

/* Allocate `count` physically contiguous pages (e.g. for a task stack --
 * a stack needs to be one contiguous region, several separate single
 * pages won't do). Returns the physical address of the first page, or 0
 * if no contiguous run of that size is free. */
uint64_t pmm_alloc_pages(uint64_t count);

void pmm_free_page(uint64_t phys_addr);

/* Frees `count` pages starting at phys_addr -- the multi-page
 * counterpart to pmm_alloc_pages(), for giving back a stack or similar
 * contiguous allocation in one call instead of a manual per-page loop
 * at every call site. */
void pmm_free_pages(uint64_t phys_addr, uint64_t count);

/* Adds a reference to an already-allocated page instead of allocating a
 * fresh one -- used by fork()'s copy-on-write page sharing. Every extra
 * pmm_page_ref() needs a matching extra pmm_free_page() before the page
 * is actually returned to the free list. */
void pmm_page_ref(uint64_t phys_addr);

/* Current owner count for a page (1 = sole owner, the normal case). 0
 * if the page isn't currently allocated at all. */
uint8_t pmm_page_refcount(uint64_t phys_addr);

void pmm_print_stats(void);
