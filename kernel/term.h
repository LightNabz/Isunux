#pragma once

/* The actual terminal a human is looking at: writes go to both the
 * on-screen framebuffer console (fb.c) and COM1 (so `make run`,
 * `-display none`, and anything tailing the host's -serial stdio still
 * see everything too). This is deliberately separate from
 * serial_print()/serial_putc() in serial.h, which are the kernel's
 * internal debug log -- COM1-only, never drawn on screen. Use this for
 * anything a real Unix console would actually show the user: shell
 * output, command output, keyboard echo. Use serial_print() for
 * kernel-internal diagnostics nobody but a developer tailing the
 * serial port should see. */

void term_putc(char c);
void term_print(const char *s);
