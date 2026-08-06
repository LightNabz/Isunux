#include "idt.h"
#include "serial.h"
#include "pic.h"
#include "kutil.h"
#include "task.h"
#include "keyboard.h"

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

            /* EOI *before* handling the scancode, not after -- mirrors
             * the timer case above. keyboard_handle_scancode() can call
             * process_signal_foreground() on Ctrl-C/Ctrl-Z, which may
             * yield() (Ctrl-Z) or loop forever on yield() (Ctrl-C, since
             * the task becomes TASK_TERMINATED and this call site is
             * never resumed). If EOI waited until after that call, as it
             * used to, the 8259 would never see it -- IRQ1's in-service
             * bit stays set and the PIC refuses to deliver any further
             * keyboard interrupts, freezing input for the rest of boot
             * (Ctrl-C) or until a SIGCONT that can now never be typed
             * (Ctrl-Z deadlocks itself this way). */
            pic_send_eoi(1);

            keyboard_handle_scancode(scancode);
            return;
        }
        default:
            /* something we haven't wired handling for yet -- still have
             * to EOI it or the PIC won't deliver any more interrupts */
            pic_send_eoi((uint8_t)irq);
            return;
    }
}
