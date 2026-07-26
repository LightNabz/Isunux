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
 * Intermediate tables are always PRESENT|WRITE; actual permissions are
 * enforced at the leaf entry, not here. */
static uint64_t get_or_create_table(uint64_t *entry) {
    if (*entry & PTE_PRESENT) {
        return *entry & ~0xFFFULL;
    }

    uint64_t new_phys = pmm_alloc_page();
    k_memset(phys_to_virt(new_phys), 0, PAGE_SIZE);
    *entry = new_phys | PTE_PRESENT | PTE_WRITE;
    return new_phys;
}

void vmm_map_4k(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    uint64_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pd_idx   = (vaddr >> 21) & 0x1FF;
    uint64_t pt_idx   = (vaddr >> 12) & 0x1FF;

    uint64_t *pml4 = phys_to_virt(kernel_pml4_phys);
    uint64_t *pdpt = phys_to_virt(get_or_create_table(&pml4[pml4_idx]));
    uint64_t *pd   = phys_to_virt(get_or_create_table(&pdpt[pdpt_idx]));
    uint64_t *pt   = phys_to_virt(get_or_create_table(&pd[pd_idx]));

    pt[pt_idx] = (paddr & ~0xFFFULL) | flags | PTE_PRESENT;
}

/* Same walk, but stops one level early and sets the PS (huge) bit on the
 * PD entry directly instead of descending into a page table. Used for
 * the HHDM window so we don't burn thousands of pages on page tables
 * just to identity-map several GiB of RAM. */
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
