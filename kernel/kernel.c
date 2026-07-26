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
#include "vmm.h"
#include "task.h"

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

__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".requests")))
static volatile struct limine_kernel_address_request kaddr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
};

static void hcf(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

/* Busy-wait a bit between prints -- purely a spin count, not tied to
 * real time. Long enough that a single call spans multiple 50ms time
 * slices, so these tasks genuinely get interrupted mid-loop instead of
 * conveniently finishing right before the next tick. */
static void busy_spin(void) {
    for (volatile uint64_t i = 0; i < 15000000; i++) { }
}

static void spinner_x(void) {
    uint64_t beat = 0;
    for (;;) {
        serial_print("  [task-x] beat ");
        serial_print_dec(beat++);
        serial_print("\n");
        busy_spin();
    }
}

static void spinner_y(void) {
    uint64_t beat = 0;
    for (;;) {
        serial_print("  [task-y] beat ");
        serial_print_dec(beat++);
        serial_print("\n");
        busy_spin();
    }
}

static void spinner_z(void) {
    uint64_t beat = 0;
    for (;;) {
        serial_print("  [task-z] beat ");
        serial_print_dec(beat++);
        serial_print("\n");
        busy_spin();
    }
}

void _start(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    serial_init();
    serial_print("========================================\n");
    serial_print(" milestone 5: preemptive round-robin\n");
    serial_print("========================================\n");

    gdt_init();
    pic_remap();
    idt_init();
    pit_init(100);
    asm volatile ("sti");
    serial_print("[ok] gdt/idt/pic/pit up, interrupts enabled\n");

    if (memmap_request.response == NULL || hhdm_request.response == NULL ||
        kaddr_request.response == NULL) {
        serial_print("!!! limine didn't answer a request we need. halting.\n");
        hcf();
    }

    pmm_init(memmap_request.response);
    serial_print("[ok] pmm initialized\n");

    uint64_t hhdm_offset = hhdm_request.response->offset;
    uint64_t kernel_phys = kaddr_request.response->physical_base;
    uint64_t kernel_virt = kaddr_request.response->virtual_base;

    vmm_init(hhdm_offset, kernel_phys, kernel_virt);
    serial_print("[ok] cr3 switched, our own page tables are active\n\n");

    task_init();
    task_create("task-x", spinner_x);
    task_create("task-y", spinner_y);
    task_create("task-z", spinner_z);
    serial_print("[ok] 3 tasks created, none of them ever call yield()\n");
    serial_print("[ok] time slice: 5 ticks (50ms). watch the timer preempt them.\n\n");

    /* main never calls yield() either from here on -- it just idles.
     * interrupts stay enabled, so the timer keeps firing, keeps calling
     * yield() from inside irq_handler, and keeps preempting whichever
     * task (including main itself) happens to be running. */
    for (;;) {
        asm volatile ("hlt");
    }
}
