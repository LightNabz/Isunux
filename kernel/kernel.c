#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "serial.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"

__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

static void hcf(void) {
    for (;;) {
        asm volatile ("hlt");
    }
}

void _start(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        for (;;) { asm volatile ("cli; hlt"); }
    }

    serial_init();
    serial_print("========================================\n");
    serial_print(" milestone 2: gdt + idt + interrupts\n");
    serial_print("========================================\n");

    serial_print("[ok] serial initialized\n");

    gdt_init();
    serial_print("[ok] gdt loaded\n");

    /* IMPORTANT: remap the PIC and build the IDT *before* ever enabling
     * interrupts with sti, or a stray IRQ could fire into an unmapped
     * vector / collide with a CPU exception vector. */
    pic_remap();
    serial_print("[ok] pic remapped: irq0-7 -> vectors 32-39, irq8-15 -> 40-47\n");

    idt_init();
    serial_print("[ok] idt loaded: 32 exception vectors + 16 irq vectors\n");

    pit_init(100); /* 100 Hz -> a tick every 10ms */
    serial_print("[ok] pit programmed for 100hz\n");

    serial_print("\nenabling interrupts now...\n\n");
    asm volatile ("sti");

    /* the timer should start ticking immediately. type on the keyboard
     * (if you're running this interactively) to see scancodes. */
    hcf();
}
