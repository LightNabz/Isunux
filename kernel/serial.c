#include "serial.h"
#include "kutil.h"
#include "limine.h"

#define COM1 0x3f8

__attribute__((used, section(".requests")))
static volatile struct limine_terminal_request terminal_request = {
    .id = LIMINE_TERMINAL_REQUEST,
    .revision = 0,
    .callback = NULL,
};

static uint64_t terminal_strlen(const char *s) {
    uint64_t len = 0;
    while (s[len]) len++;
    return len;
}

static void terminal_putc(char c) {
    if (!terminal_request.response || terminal_request.response->terminal_count == 0) return;
    struct limine_terminal *term = terminal_request.response->terminals[0];
    if (!term || !terminal_request.response->write) return;
    char buf[1] = { c };
    terminal_request.response->write(term, buf, 1);
}

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
    terminal_putc(c);
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
