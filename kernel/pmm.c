#include "pmm.h"
#include "serial.h"
#include "kutil.h"

/* Static bitmap covering PMM_TRACKED_PHYS_MEMORY of physical RAM (see
 * pmm.h). That's plenty for a hobby kernel running under QEMU, and a
 * static array means we don't need a working heap or HHDM access to
 * stand this up -- deliberately the simplest version that works. */
#define MAX_PAGES         (PMM_TRACKED_PHYS_MEMORY / PAGE_SIZE)
#define BITMAP_SIZE_BYTES (MAX_PAGES / 8)

static uint8_t bitmap[BITMAP_SIZE_BYTES];

static uint64_t total_pages = 0; /* pages within the tracked range, usable or not */
static uint64_t free_pages  = 0;
static uint64_t used_pages  = 0;

/* where to start scanning for the next free page -- pure optimization so
 * we don't rescan low, permanently-used memory on every allocation */
static uint64_t scan_hint = 0;

static inline void bitmap_set(uint64_t page_idx) {
    bitmap[page_idx / 8] |= (uint8_t)(1u << (page_idx % 8));
}

static inline void bitmap_clear(uint64_t page_idx) {
    bitmap[page_idx / 8] &= (uint8_t)~(1u << (page_idx % 8));
}

static inline int bitmap_test(uint64_t page_idx) {
    return (bitmap[page_idx / 8] >> (page_idx % 8)) & 1;
}

static void mark_region_free(uint64_t base, uint64_t length) {
    /* round the start UP to a page boundary and the end DOWN to one --
     * we never want to claim a partial page as free */
    uint64_t start = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t end = (base + length) & ~(PAGE_SIZE - 1);

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint64_t idx = addr / PAGE_SIZE;
        if (idx >= MAX_PAGES) break; /* past what our static bitmap covers */
        if (bitmap_test(idx)) {
            bitmap_clear(idx);
            free_pages++;
        }
    }
}

void pmm_init(struct limine_memmap_response *memmap) {
    /* start out assuming everything is used/unavailable; usable regions
     * get cleared below. this way anything Limine doesn't explicitly
     * call USABLE (kernel image, reserved, ACPI, MMIO holes, etc.) stays
     * marked used and pmm_alloc_page() will never hand it out. */
    k_memset(bitmap, 0xFF, sizeof(bitmap));

    uint64_t highest_addr = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        uint64_t end = entry->base + entry->length;
        if (end > highest_addr) highest_addr = end;

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            mark_region_free(entry->base, entry->length);
        }
    }

    total_pages = highest_addr / PAGE_SIZE;
    if (total_pages > MAX_PAGES) total_pages = MAX_PAGES;
    used_pages = total_pages - free_pages;
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t idx = scan_hint; idx < total_pages; idx++) {
        if (!bitmap_test(idx)) {
            bitmap_set(idx);
            free_pages--;
            used_pages++;
            scan_hint = idx + 1;
            return idx * PAGE_SIZE;
        }
    }
    /* hint may have skipped over pages freed behind it -- do one full
     * sweep from the start before giving up */
    for (uint64_t idx = 0; idx < scan_hint; idx++) {
        if (!bitmap_test(idx)) {
            bitmap_set(idx);
            free_pages--;
            used_pages++;
            scan_hint = idx + 1;
            return idx * PAGE_SIZE;
        }
    }
    return 0; /* out of memory */
}

uint64_t pmm_alloc_pages(uint64_t count) {
    if (count == 0 || count > total_pages) return 0;

    for (uint64_t start = 0; start + count <= total_pages; start++) {
        uint64_t j = 0;
        for (; j < count; j++) {
            if (bitmap_test(start + j)) break; /* not free, this run doesn't work */
        }
        if (j == count) {
            for (uint64_t k = 0; k < count; k++) {
                bitmap_set(start + k);
            }
            free_pages -= count;
            used_pages += count;
            return start * PAGE_SIZE;
        }
        /* skip past the page that failed, no point rechecking it */
        start += j;
    }
    return 0; /* no contiguous run big enough */
}

void pmm_free_page(uint64_t phys_addr) {
    uint64_t idx = phys_addr / PAGE_SIZE;
    if (idx >= total_pages) return; /* not a page we're tracking */
    if (!bitmap_test(idx)) return;  /* double free -- ignore rather than corrupt counters */

    bitmap_clear(idx);
    free_pages++;
    used_pages--;

    if (idx < scan_hint) scan_hint = idx; /* next alloc should reuse this */
}

void pmm_print_stats(void) {
    serial_print("[pmm] total: ");
    serial_print_dec(total_pages);
    serial_print(" pages (");
    serial_print_dec((total_pages * PAGE_SIZE) / (1024 * 1024));
    serial_print(" MiB tracked)\n");

    serial_print("[pmm] free:  ");
    serial_print_dec(free_pages);
    serial_print(" pages (");
    serial_print_dec((free_pages * PAGE_SIZE) / (1024 * 1024));
    serial_print(" MiB)\n");

    serial_print("[pmm] used:  ");
    serial_print_dec(used_pages);
    serial_print(" pages (");
    serial_print_dec((used_pages * PAGE_SIZE) / (1024 * 1024));
    serial_print(" MiB)\n");
}
