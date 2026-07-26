#pragma once
#include <stdint.h>

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITE   (1ULL << 1)
#define PTE_HUGE    (1ULL << 7) /* PS bit: this entry is a 2MiB/1GiB page, not a pointer to the next level */

/* Build our own page tables (mapping the HHDM window + the kernel image),
 * then switch CR3 to them. hhdm_offset/kernel_phys_base/kernel_virt_base
 * come straight from Limine's requests. */
void vmm_init(uint64_t hhdm_offset, uint64_t kernel_phys_base, uint64_t kernel_virt_base);

/* So other subsystems (e.g. the task manager, building stacks out of
 * PMM-allocated physical pages) can reuse the same HHDM offset without
 * re-deriving it. */
uint64_t vmm_hhdm_offset(void);

/* Map a single 4KiB page in our kernel address space. Allocates any
 * intermediate page tables it needs via the PMM. */
void vmm_map_4k(uint64_t vaddr, uint64_t paddr, uint64_t flags);
