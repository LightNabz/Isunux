#pragma once
#include <stdint.h>

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITE   (1ULL << 1)
#define PTE_USER    (1ULL << 2) /* U/S bit: without this, ring 3 gets a page fault touching this page at all */
#define PTE_HUGE    (1ULL << 7) /* PS bit: this entry is a 2MiB/1GiB page, not a pointer to the next level */

/* Build our own page tables (mapping the HHDM window + the kernel image),
 * then switch CR3 to them. hhdm_offset/kernel_phys_base/kernel_virt_base
 * come straight from Limine's requests. */
void vmm_init(uint64_t hhdm_offset, uint64_t kernel_phys_base, uint64_t kernel_virt_base);

/* So other subsystems (e.g. the task manager, building stacks out of
 * PMM-allocated physical pages) can reuse the same HHDM offset without
 * re-deriving it. */
uint64_t vmm_hhdm_offset(void);

/* The physical address of the kernel's own PML4 -- used as the "source
 * of truth" higher half that every new address space inherits from. */
uint64_t vmm_kernel_pml4(void);

/* Map a single 4KiB page within a *specific* address space (any PML4
 * physical address -- the kernel's own, or one from
 * vmm_new_address_space()). Allocates any intermediate page tables it
 * needs via the PMM. */
void vmm_map_4k_in(uint64_t pml4_phys, uint64_t vaddr, uint64_t paddr, uint64_t flags);

/* Convenience wrapper: maps into the kernel's own address space, same
 * as the milestone-4 vmm_map_4k did. */
void vmm_map_4k(uint64_t vaddr, uint64_t paddr, uint64_t flags);

/* Allocate a brand new PML4 that shares the kernel's higher half (HHDM +
 * kernel image, PML4 indices 256-511) but starts with a completely
 * empty lower half. That empty lower half is what makes it a genuinely
 * separate address space -- a process maps its own code/stack into
 * indices 0-255 without touching anything another process (or the
 * kernel) can see down there. */
uint64_t vmm_new_address_space(void);

/* Switch CR3 to a given address space (the kernel's own, or one from
 * vmm_new_address_space()). */
void vmm_activate(uint64_t pml4_phys);
