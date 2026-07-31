#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "limine.h"

/* Framebuffer text console. Call fb_init() once, as early as possible,
 * with the response from a limine_framebuffer_request. After that,
 * fb_putc()/fb_print() draw an 8x16-glyph terminal (cursor, \n \r \b,
 * line wrap, and scroll-on-overflow) directly into the linear
 * framebuffer Limine handed us. If fb_init() was never called (or the
 * bootloader gave us no framebuffer), fb_putc()/fb_print() are no-ops
 * so callers never have to check fb_available() themselves. */

void fb_init(struct limine_framebuffer_response *response);
bool fb_available(void);
void fb_putc(char c);
void fb_print(const char *s);
void fb_clear(void);
