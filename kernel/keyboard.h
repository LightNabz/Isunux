#pragma once
#include <stdint.h>

/* Called from irq_handler on every keyboard interrupt with the raw
 * scancode (make or break code, untranslated). */
void keyboard_handle_scancode(uint8_t sc);

/* Blocks (cooperatively yielding) until at least one byte is
 * available, then drains up to `count` bytes or a newline, whichever
 * comes first -- classic line-buffered "cooked mode" read. Always
 * returns > 0; there's no EOF concept for a live keyboard. */
long keyboard_read(void *buf, uint64_t count);
