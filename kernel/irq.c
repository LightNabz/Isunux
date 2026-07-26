#include "idt.h"
#include "serial.h"
#include "pic.h"
#include "kutil.h"
#include "task.h"

static volatile uint64_t timer_ticks = 0;

#define KEYBOARD_DATA_PORT 0x60

/* How many timer ticks a task gets before we preempt it. PIT is running
 * at 100Hz (see pit_init in kernel.c), so this is a 50ms time slice. */
#define TIME_SLICE_TICKS 5

void irq_handler(interrupt_frame_t *frame) {
    uint64_t irq = frame->int_no - 32;

    switch (irq) {
        case 0: { /* PIT timer */
            timer_ticks++;

            /* Send EOI *before* any possible context switch below --
             * yield() might not return to this exact call site for a
             * while (or ever, if this task never runs again), and the
             * PIC needs to know this IRQ was handled well before that. */
            pic_send_eoi(0);

            if (timer_ticks % 100 == 0) { /* once a second */
                serial_print("[timer] ");
                serial_print_dec(timer_ticks / 100);
                serial_print("s uptime, ");
                serial_print_dec(timer_ticks);
                serial_print(" ticks\n");
            }

            if (timer_ticks % TIME_SLICE_TICKS == 0) {
                /* this is the entire preemption mechanism: yield() does
                 * not know or care that it's being called from inside
                 * an interrupt handler instead of voluntarily. the call
                 * chain (irq_common -> irq_handler -> yield ->
                 * switch_context) unwinds itself correctly through
                 * iretq whenever this task gets resumed later. */
                yield();
            }
            return;
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
            pic_send_eoi(1);
            return;
        }
        default:
            /* something we haven't wired handling for yet -- still have
             * to EOI it or the PIC won't deliver any more interrupts */
            pic_send_eoi((uint8_t)irq);
            return;
    }
}
