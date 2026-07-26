#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "serial.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "pmm.h"

__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
};

static void hcf(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

static void print_memmap(struct limine_memmap_response *memmap) {
    static const char *type_names[] = {
        "usable", "reserved", "acpi reclaimable", "acpi nvs",
        "bad memory", "bootloader reclaimable", "kernel/modules", "framebuffer",
    };

    serial_print("[memmap] ");
    serial_print_dec(memmap->entry_count);
    serial_print(" regions from limine:\n");

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        serial_print("  base ");
        serial_print_hex(e->base);
        serial_print("  len ");
        serial_print_hex(e->length);
        serial_print("  ");
        serial_print(e->type < 8 ? type_names[e->type] : "unknown");
        serial_print("\n");
    }
}

void _start(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    serial_init();
    serial_print("========================================\n");
    serial_print(" milestone 3: physical memory manager\n");
    serial_print("========================================\n");

    gdt_init();
    pic_remap();
    idt_init();
    pit_init(100);
    asm volatile ("sti");
    serial_print("[ok] gdt/idt/pic/pit up, interrupts enabled\n\n");

    if (memmap_request.response == NULL) {
        serial_print("!!! limine did not answer the memmap request. halting.\n");
        hcf();
    }

    print_memmap(memmap_request.response);
    serial_print("\n");

    pmm_init(memmap_request.response);
    serial_print("[ok] pmm initialized\n");
    pmm_print_stats();

    serial_print("\n--- allocation test ---\n");

    uint64_t pages[5];
    for (int i = 0; i < 5; i++) {
        pages[i] = pmm_alloc_page();
        serial_print("alloc -> ");
        serial_print_hex(pages[i]);
        serial_print("\n");
    }

    serial_print("\nfreeing page[2] (");
    serial_print_hex(pages[2]);
    serial_print(")...\n");
    pmm_free_page(pages[2]);

    uint64_t reused = pmm_alloc_page();
    serial_print("alloc -> ");
    serial_print_hex(reused);
    if (reused == pages[2]) {
        serial_print("  <- reused the freed page, allocator works.\n");
    } else {
        serial_print("  <- did NOT reuse the freed page, something's off.\n");
    }

    serial_print("\nstats after the test:\n");
    pmm_print_stats();

    serial_print("\nmilestone 3 core allocator: done.\n");
    hcf();
}
