#pragma once
#include <stdint.h>

/* Called from irq_handler on every keyboard interrupt with the raw
 * scancode (make or break code, untranslated). */
void keyboard_handle_scancode(uint8_t sc);

/* Blocks (cooperatively yielding) until at least one byte is
 * available. In canonical mode (the default), drains up to `count`
 * bytes or a newline, whichever comes first -- classic line-buffered
 * "cooked mode" read, since a whole line only ever becomes available
 * atomically on Enter (see commit_char()). In raw mode, every
 * translated keystroke is queued immediately, so this just drains
 * whatever's already there up to `count` bytes with no newline
 * significance at all. Always returns > 0; there's no EOF concept for
 * a live keyboard. */
long keyboard_read(void *buf, uint64_t count);

/* Switches between canonical (line-buffered, in-driver backspace
 * editing, whole lines delivered atomically on Enter) and raw (every
 * translated keystroke delivered immediately, no local editing at
 * all) input. Global, not per-fd -- there's one physical console and
 * one foreground process at a time already (see
 * process_set_foreground), so a single global mode is consistent with
 * that and meaningfully simpler than per-session tty state. Ctrl-C/
 * Ctrl-Z keep working in either mode -- unlike real termios (where
 * ISIG can be disabled independently via cfmakeraw()), that's not
 * offered here on purpose: a bug in a raw-mode program should never
 * be able to make the keyboard unreachable. Switching modes discards
 * any partially-typed line still sitting in the canonical-mode edit
 * buffer. */
void tty_set_raw(int enable);

/* Toggles local echo -- whether a keystroke gets drawn to the
 * terminal at all, independent of canonical/raw mode (so canonical
 * mode with echo off is a real, useful combination: line editing --
 * backspace, buffering until Enter -- still happens, nothing is drawn,
 * which is the traditional shape of a password-style prompt). Ctrl-C/
 * Ctrl-Z's own "^C"/"^Z" feedback is deliberately NOT gated by this --
 * it always prints, regardless of echo state, since knowing your
 * foreground process just got killed or stopped is worth seeing even
 * mid-password-entry. */
void tty_set_echo(int enable);
