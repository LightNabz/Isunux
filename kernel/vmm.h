#pragma once
#include <stdint.h>

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITE   (1ULL << 1)
#define PTE_USER    (1ULL << 2) /* U/S bit: without this, ring 3 gets a page fault touching this page at all */
#define PTE_HUGE    (1ULL << 7) /* PS bit: this entry is a 2MiB/1GiB page, not a pointer to the next level */

/* Bits 9-11 of a PTE are defined by the x86-64 spec as always-available
 * for OS use -- the CPU never reads or writes them itself. We use bit 9
 * to mark a page that's shared read-only between two (or more) address
 * spaces because of fork(), but was WRITABLE before the fork -- i.e. a
 * genuine copy-on-write page, as opposed to a page that's simply,
 * permanently read-only (like a code segment) and needs no special
 * handling on a write fault beyond "that's a real segfault". See
 * exception_handler's vector-14 case in exceptions.c. */
#define PTE_COW     (1ULL << 9)

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

/* Shares every present mapping in the LOW half (PML4 indices 0-255 --
 * canonical-low, userspace territory) of src_pml4 with dest_pml4,
 * instead of the old full-copy behavior: a writable page gets its
 * PTE_WRITE cleared and PTE_COW set in BOTH the parent's own table and
 * the child's new one, then pmm_page_ref()'d so the physical page now
 * has two owners -- the actual copy is deferred to whichever side
 * writes to it first (see exception_handler's vector-14 case). A page
 * that's already permanently read-only (e.g. a code segment) is just
 * shared directly with no COW marking, since neither side can
 * legitimately write to it anyway. Reloads the parent's own CR3 once
 * the whole walk is done, since it just downgraded some of the
 * parent's own live PTEs and any cached writable TLB entries for them
 * need to go. This is fork()'s entire address-space story. */
void vmm_clone_lower_half(uint64_t dest_pml4_phys, uint64_t src_pml4_phys);

/* Walks pml4_phys down to the leaf PTE for vaddr, WITHOUT creating any
 * missing intermediate table (unlike vmm_map_4k_in, which always
 * creates on demand) -- this is a pure lookup for the page fault
 * handler, which needs to inspect (and sometimes modify in place) an
 * existing mapping's flags. Returns NULL if any level of the walk is
 * missing, or if the entry found is a 2MiB huge page (userspace never
 * uses those, so a leaf lookup should never land on one). The returned
 * pointer is a live pointer into the table itself -- writes through it
 * take effect immediately, same caller-must-flush-the-TLB caveat as
 * vmm_clone_lower_half. */
uint64_t *vmm_get_pte(uint64_t pml4_phys, uint64_t vaddr);

/* Invalidates a single page's TLB entry (INVLPG) -- narrower and
 * cheaper than vmm_activate()'s full CR3-reload flush, for the page
 * fault handler's single-page remap case. */
void vmm_invalidate_page(uint64_t vaddr);

/* Switch CR3 to a given address space (the kernel's own, or one from
 * vmm_new_address_space()). */
void vmm_activate(uint64_t pml4_phys);

/* Tears down everything vmm_new_address_space() + vmm_clone_lower_half()
 * (or vmm_map_4k_in()) built: walks the LOW half only (PML4 indices
 * 0-255 -- see vmm_clone_lower_half's comment on why the high half is
 * never touched, it's the shared kernel/HHDM tables), pmm_free_page()-ing
 * every present leaf mapping, then every now-empty PT/PD/PDPT page on
 * the way back up, then finally the PML4 page itself. Caller must not
 * still be running under this address space -- switch CR3 elsewhere
 * (vmm_activate) first, same ordering rule as freeing your own stack. */
void vmm_destroy_address_space(uint64_t pml4_phys);
