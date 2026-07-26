#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "serial.h"
#include "gdt.h"
#include "idt.h"

/* Tell Limine we're using base protocol revision 2. */
__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

/* Request/response region markers, required by the Limine boot protocol. */
__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

static void hcf(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void _start(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    serial_init();
    serial_print("========================================\n");
    serial_print(" milestone 2: gdt + idt + exceptions\n");
    serial_print("========================================\n");

    serial_print("[ok] serial initialized\n");

    gdt_init();
    serial_print("[ok] gdt loaded (our own, not limine's)\n");

    idt_init();
    serial_print("[ok] idt loaded, 32 exception vectors wired\n");

    serial_print("\nabout to trigger a real divide-by-zero on purpose...\n");

    /* volatile so the compiler can't fold this at compile time -- we want
     * an actual runtime #DE (vector 0) to prove the handler catches it. */
    volatile uint64_t a = 42;
    volatile uint64_t b = 0;
    volatile uint64_t c = a / b;
    (void)c;

    /* should never get here */
    serial_print("if you see this, the exception did NOT fire. something's wrong.\n");
    hcf();
}
