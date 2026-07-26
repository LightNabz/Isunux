#include "idt.h"
#include "serial.h"
#include "pic.h"
#include "kutil.h"

static volatile uint64_t timer_ticks = 0;

#define KEYBOARD_DATA_PORT 0x60

void irq_handler(interrupt_frame_t *frame) {
    uint64_t irq = frame->int_no - 32;

    switch (irq) {
        case 0: { /* PIT timer */
            timer_ticks++;
            if (timer_ticks % 100 == 0) { /* pit_init is set to 100 Hz -> once a second */
                serial_print("[timer] ");
                serial_print_dec(timer_ticks / 100);
                serial_print("s uptime, ");
                serial_print_dec(timer_ticks);
                serial_print(" ticks\n");
            }
            break;
        }
        case 1: { /* keyboard */
            uint8_t scancode = inb(KEYBOARD_DATA_PORT);
            serial_print("[kbd] scancode ");
            serial_print_hex(scancode);
            if (scancode & 0x80) {
                serial_print("  (key released)\n");
            } else {
                serial_print("  (key pressed)\n");
            }
            break;
        }
        default:
            /* something we haven't wired handling for yet -- still have
             * to EOI it or the PIC won't deliver any more interrupts */
            break;
    }

    pic_send_eoi((uint8_t)irq);
}
