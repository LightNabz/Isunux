#pragma once
#include <stdint.h>
#include "limine.h"

#define PAGE_SIZE 4096ULL

void pmm_init(struct limine_memmap_response *memmap);

/* Returns the physical address of a freshly allocated 4KiB page, or 0 if
 * we're out of memory. 0 is never a valid allocation because page 0 is
 * always marked reserved (it's the real-mode IVT/BDA). */
uint64_t pmm_alloc_page(void);

void pmm_free_page(uint64_t phys_addr);

void pmm_print_stats(void);
