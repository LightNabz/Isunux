#include "term.h"
#include "serial.h"
#include "fb.h"

void term_putc(char c) {
    serial_putc(c); /* reuse the COM1 primitive -- it's just a hardware
                      * byte write at this point, nothing debug-specific
                      * about the byte itself, only about who calls it */
    fb_putc(c);
}

void term_print(const char *s) {
    for (unsigned long i = 0; s[i] != '\0'; i++) {
        term_putc(s[i]);
    }
}
