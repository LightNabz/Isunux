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

static void task_a_entry(void) {
    for (int i = 0; i < 3; i++) {
        serial_print("  [task-a] iteration ");
        serial_print_dec(i);
        serial_print(", yielding...\n");
        yield();
    }
    serial_print("  [task-a] loop done, returning\n");
}

static void task_b_entry(void) {
    for (int i = 0; i < 3; i++) {
        serial_print("  [task-b] iteration ");
        serial_print_dec(i);
        serial_print(", yielding...\n");
        yield();
    }
    serial_print("  [task-b] loop done, returning\n");
}

void _start(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    serial_init();
    serial_print("========================================\n");
    serial_print(" milestone 5: tasks + context switch\n");
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
    serial_print("[ok] task_init: current flow of execution is now 'main'\n");

    task_create("task-a", task_a_entry);
    task_create("task-b", task_b_entry);
    serial_print("[ok] created task-a and task-b (not running yet)\n\n");

    serial_print("--- main manually yielding, round-robin should ping-pong ---\n\n");

    /* main hands off to whichever task is next in the ring each time it
     * calls yield(). Once both tasks have run to completion (each is a
     * short loop that yields a few times then returns), yield() becomes
     * a no-op again because main is the only READY task left. */
    for (int i = 0; i < 8; i++) {
        yield();
    }

    serial_print("\nmain: done yielding. milestone 5 step 1+2: proven.\n");
    hcf();
}
