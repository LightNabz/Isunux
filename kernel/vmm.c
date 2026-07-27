#include "vmm.h"
#include "pmm.h"
#include "kutil.h"

#define ENTRIES_PER_TABLE 512
#define HUGE_PAGE_SIZE    0x200000ULL /* 2 MiB */

static uint64_t hhdm_offset_g = 0;
static uint64_t kernel_pml4_phys = 0;

extern char kernel_virt_start[];
extern char kernel_virt_end[];

static inline uint64_t *phys_to_virt(uint64_t phys) {
    return (uint64_t *)(phys + hhdm_offset_g);
}

/* Given a slot in a table (a PML4/PDPT/PD entry that should point to the
 * next level down), return the physical address of that next-level
 * table -- allocating and zeroing a fresh one if the slot is empty.
 *
 * Intermediate tables are always PRESENT|WRITE|USER, regardless of what
 * the final leaf mapping will be. x86 paging ANDs the U/S bit down the
 * whole walk, so a 0 anywhere above the leaf blocks ring 3 no matter
 * what the leaf says -- but leaving U=1 up here doesn't grant ring 3
 * anything by itself. The leaf entry's own flags are what actually
 * decide whether a page is user-accessible, so that's the only place
 * that needs to make the real decision. */
static uint64_t get_or_create_table(uint64_t *entry) {
    if (*entry & PTE_PRESENT) {
        return *entry & ~0xFFFULL;
    }

    uint64_t new_phys = pmm_alloc_page();
    k_memset(phys_to_virt(new_phys), 0, PAGE_SIZE);
    *entry = new_phys | PTE_PRESENT | PTE_WRITE | PTE_USER;
    return new_phys;
}

void vmm_map_4k_in(uint64_t pml4_phys, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    uint64_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pd_idx   = (vaddr >> 21) & 0x1FF;
    uint64_t pt_idx   = (vaddr >> 12) & 0x1FF;

    uint64_t *pml4 = phys_to_virt(pml4_phys);
    uint64_t *pdpt = phys_to_virt(get_or_create_table(&pml4[pml4_idx]));
    uint64_t *pd   = phys_to_virt(get_or_create_table(&pdpt[pdpt_idx]));
    uint64_t *pt   = phys_to_virt(get_or_create_table(&pd[pd_idx]));

    pt[pt_idx] = (paddr & ~0xFFFULL) | flags | PTE_PRESENT;
}

void vmm_map_4k(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    vmm_map_4k_in(kernel_pml4_phys, vaddr, paddr, flags);
}

/* Same walk, but stops one level early and sets the PS (huge) bit on the
 * PD entry directly instead of descending into a page table. Used only
 * for the kernel's own HHDM window, so it always targets the kernel's
 * PML4 directly rather than taking one as a parameter. */
static void map_2m(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    uint64_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pd_idx   = (vaddr >> 21) & 0x1FF;

    uint64_t *pml4 = phys_to_virt(kernel_pml4_phys);
    uint64_t *pdpt = phys_to_virt(get_or_create_table(&pml4[pml4_idx]));
    uint64_t *pd   = phys_to_virt(get_or_create_table(&pdpt[pdpt_idx]));

    pd[pd_idx] = (paddr & ~(HUGE_PAGE_SIZE - 1)) | flags | PTE_PRESENT | PTE_HUGE;
}

void vmm_init(uint64_t hhdm_offset, uint64_t kernel_phys_base, uint64_t kernel_virt_base) {
    hhdm_offset_g = hhdm_offset;

    kernel_pml4_phys = pmm_alloc_page();
    k_memset(phys_to_virt(kernel_pml4_phys), 0, PAGE_SIZE);

    /* Map the whole HHDM window with 2MiB huge pages, covering exactly
     * as much physical memory as the PMM tracks. This is what lets our
     * own table-walking code (phys_to_virt above) keep working after we
     * switch CR3 -- our tables map the same HHDM offset Limine's did. */
    for (uint64_t phys = 0; phys < PMM_TRACKED_PHYS_MEMORY; phys += HUGE_PAGE_SIZE) {
        map_2m(hhdm_offset + phys, phys, PTE_WRITE);
    }

    /* Map the kernel image itself at its higher-half virtual address,
     * page by page, using the exact size from the linker script symbols
     * -- not a guessed round number. */
    uint64_t kernel_size = (uint64_t)kernel_virt_end - (uint64_t)kernel_virt_start;
    kernel_size = (kernel_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint64_t off = 0; off < kernel_size; off += PAGE_SIZE) {
        vmm_map_4k(kernel_virt_base + off, kernel_phys_base + off, PTE_WRITE);
    }

    asm volatile ("mov %0, %%cr3" : : "r"(kernel_pml4_phys) : "memory");
}

uint64_t vmm_hhdm_offset(void) {
    return hhdm_offset_g;
}

uint64_t vmm_kernel_pml4(void) {
    return kernel_pml4_phys;
}

uint64_t vmm_new_address_space(void) {
    uint64_t new_phys = pmm_alloc_page();
    uint64_t *new_pml4 = phys_to_virt(new_phys);
    k_memset(new_pml4, 0, PAGE_SIZE);

    /* PML4 indices 0-255 cover virtual addresses 0x0000000000000000 -
     * 0x00007FFFFFFFFFFF (canonical low half -- userspace territory).
     * Indices 256-511 cover 0xFFFF800000000000 and up (canonical high
     * half -- where we deliberately put the HHDM window and the kernel
     * image back in milestone 1/4). Copying just those top 256 entries
     * gives the new address space the exact same kernel/HHDM mappings
     * as everyone else, sharing the underlying page tables rather than
     * duplicating them -- and leaves the entire low half zeroed, ready
     * for a process to map its own code and stack into. */
    uint64_t *kernel_pml4 = phys_to_virt(kernel_pml4_phys);
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    return new_phys;
}

void vmm_activate(uint64_t pml4_phys) {
    asm volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}
