#include "serial.h"
#include "kutil.h"
#include "limine.h"
#include "fb.h"

#define COM1 0x3f8

/* NOTE: this used to also poke the Limine *Terminal* protocol
 * (LIMINE_TERMINAL_REQUEST), but that protocol is deprecated in the
 * Limine revision this kernel targets and its response never actually
 * comes back non-NULL here -- so that code path was silently dead.
 * On-screen output now goes through fb.c (the framebuffer protocol),
 * which fb_init() sets up during early boot; see kernel.c. */

void serial_init(void) {
    outb(COM1 + 1, 0x00); /* disable interrupts */
    outb(COM1 + 3, 0x80); /* enable DLAB */
    outb(COM1 + 0, 0x03); /* divisor low byte -> 38400 baud */
    outb(COM1 + 1, 0x00); /* divisor high byte */
    outb(COM1 + 3, 0x03); /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7); /* enable FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

static int serial_tx_ready(void) {
    return inb(COM1 + 5) & 0x20;
}

void serial_putc(char c) {
    while (!serial_tx_ready()) { }
    outb(COM1, (uint8_t)c);
    fb_putc(c);
}

void serial_print(const char *s) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\n') serial_putc('\r');
        serial_putc(s[i]);
    }
}

void serial_print_hex(uint64_t val) {
    const char *digits = "0123456789abcdef";
    serial_print("0x");
    int started = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (val >> shift) & 0xF;
        if (nibble != 0 || started || shift == 0) {
            serial_putc(digits[nibble]);
            started = 1;
        }
    }
}

void serial_print_dec(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (val == 0) {
        serial_putc('0');
        return;
    }
    while (val > 0 && i > 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    serial_print(&buf[i]);
}
