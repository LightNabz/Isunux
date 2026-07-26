#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"

/* Tell Limine we're using base protocol revision 2 */
__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

/* Request/response region markers, required by the Limine boot protocol */
__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

/* ---- tiny serial (UART 16550) driver, COM1 ---- */

#define COM1 0x3f8

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void serial_init(void) {
    outb(COM1 + 1, 0x00); /* disable interrupts */
    outb(COM1 + 3, 0x80); /* enable DLAB (set baud rate divisor) */
    outb(COM1 + 0, 0x03); /* divisor low byte -> 38400 baud */
    outb(COM1 + 1, 0x00); /* divisor high byte */
    outb(COM1 + 3, 0x03); /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7); /* enable FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

static int serial_tx_ready(void) {
    return inb(COM1 + 5) & 0x20;
}

static void serial_putc(char c) {
    while (!serial_tx_ready()) { }
    outb(COM1, (uint8_t)c);
}

static void serial_print(const char *s) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        serial_putc(s[i]);
    }
}

/* halt-catch-fire: spin forever with interrupts off */
static void hcf(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void _start(void) {
    /* Bail out if the bootloader doesn't speak the revision we asked for */
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    serial_init();
    serial_print("========================================\n");
    serial_print(" hello from ring 0 OwO \n");
    serial_print(" (a from-scratch x86_64 kernel, booted\n");
    serial_print("  via Limine, saying hi over COM1)\n");
    serial_print("========================================\n");

    hcf();
}
