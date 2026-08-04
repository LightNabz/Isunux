#include "keyboard.h"
#include "term.h"
#include "task.h"
#include "process.h"
#include "syscall.h"

/* Set 1 scancodes, US QWERTY. Unmapped entries (0) are keys we don't
 * translate at all -- Ctrl, Alt, F-keys, arrows, etc. Indexed with
 * designated initializers so the table reads as "scancode -> char"
 * directly, matching the well-known Set 1 layout without needing a
 * giant positional list. */
static const char lower_table[128] = {
    [0x01] = 27, /* ESC */
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']',
    [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
};

static const char upper_table[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+', [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}',
    [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x39] = ' ',
};

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_LCTRL  0x1D /* left Ctrl only -- right Ctrl arrives as an 0xE0-prefixed extended scancode, and this driver doesn't handle extended prefixes at all, same scope cut as the arrow keys/F-keys/etc. already being unmapped */
static int shift_held = 0;
static int ctrl_held = 0;

/* the line currently being typed -- edited locally (backspace works
 * here) before ever becoming visible to a reader */
#define EDIT_LINE_MAX 256
static char edit_line[EDIT_LINE_MAX];
static uint32_t edit_len = 0;

/* completed lines only get pushed here, and only as a whole line at a
 * time (on Enter) -- this is what keyboard_read() actually drains */
#define KBD_QUEUE_SIZE 1024
static char kbd_queue[KBD_QUEUE_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;

/* Dedicated tag for task_block()/task_wake() -- its own address is the
 * "channel", its value is never read. Kept separate from kbd_queue
 * itself just so the wait/wake relationship reads clearly at each call
 * site rather than reusing a buffer's address for an unrelated purpose. */
static int kbd_wait_chan;

static void kbd_queue_push(char c) {
    uint32_t next = (kbd_head + 1) % KBD_QUEUE_SIZE;
    if (next == kbd_tail) return; /* queue full -- drop it rather than corrupt anything */
    kbd_queue[kbd_head] = c;
    kbd_head = next;
}

static int kbd_queue_pop(char *out) {
    if (kbd_tail == kbd_head) return 0; /* empty */
    *out = kbd_queue[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_QUEUE_SIZE;
    return 1;
}

static void commit_char(char c) {
    if (c == '\b') {
        if (edit_len > 0) {
            edit_len--;
            term_print("\b \b"); /* move back, blank the character, move back again */
        }
        return;
    }

    if (c == '\n') {
        term_putc('\n'); /* echo the newline itself */
        for (uint32_t i = 0; i < edit_len; i++) kbd_queue_push(edit_line[i]);
        kbd_queue_push('\n');
        edit_len = 0;
        task_wake(&kbd_wait_chan); /* a keyboard_read() blocked below might be waiting on exactly this line */
        return;
    }

    if (edit_len < EDIT_LINE_MAX - 1) {
        edit_line[edit_len++] = c;
        term_putc(c); /* echo what was typed -- "cooked mode" */
    }
}

void keyboard_handle_scancode(uint8_t sc) {
    int released = sc & 0x80;
    uint8_t code = sc & 0x7F;

    if (code == SC_LSHIFT || code == SC_RSHIFT) {
        shift_held = !released;
        return;
    }
    if (code == SC_LCTRL) {
        ctrl_held = !released;
        return;
    }

    if (released) return; /* only make codes matter for everything else */

    if (ctrl_held) {
        /* job control's whole reason for existing: these need to
         * interrupt whatever's running RIGHT NOW, mid-line, not wait for
         * Enter and go through the normal buffered-line path the way
         * ordinary typing does. process_signal_foreground() (process.c)
         * is safe to call from here, IRQ context -- it may yield()
         * internally, and a yield() from inside an interrupt handler is
         * already an established pattern in this kernel (irq.c's timer
         * preemption does exactly this). */
        if (code == 0x2E) { /* 'C' key position */
            term_print("^C\n");
            process_signal_foreground(SIGINT);
            return;
        }
        if (code == 0x2C) { /* 'Z' key position */
            term_print("^Z\n");
            process_signal_foreground(SIGTSTP);
            return;
        }
        return; /* other Ctrl+key combos: not handled, silently ignored, same as any other unmapped key */
    }

    char c = shift_held ? upper_table[code] : lower_table[code];
    if (c == 0) return; /* unmapped key (alt, F-keys, arrows, ...) */

    commit_char(c);
}

long keyboard_read(void *buf, uint64_t count) {
    uint8_t *dst = (uint8_t *)buf;
    uint64_t n = 0;

    char c;
    while (!kbd_queue_pop(&c)) {
        task_block(&kbd_wait_chan); /* woken the instant a full line lands, instead of polling every scheduler turn */
    }
    dst[n++] = (uint8_t)c;

    /* whole lines are pushed atomically on Enter, so once we've gotten
     * one byte it's safe to keep draining without blocking again until
     * we hit the newline or run out of requested space */
    while (n < count && c != '\n' && kbd_queue_pop(&c)) {
        dst[n++] = (uint8_t)c;
    }

    return (long)n;
}
