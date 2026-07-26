#include "pic.h"
#include "kutil.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

#define PIC1_VECTOR_OFFSET 0x20 /* IRQ0-7  -> interrupt vectors 32-39 */
#define PIC2_VECTOR_OFFSET 0x28 /* IRQ8-15 -> interrupt vectors 40-47 */

void pic_remap(void) {
    /* ICW1: start init sequence, tell it ICW4 will follow */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    /* ICW2: vector offsets, this is the actual remap */
    outb(PIC1_DATA, PIC1_VECTOR_OFFSET);
    outb(PIC2_DATA, PIC2_VECTOR_OFFSET);

    /* ICW3: wire the two PICs together (master sees slave on IRQ2) */
    outb(PIC1_DATA, 0x04); /* bit 2 set: slave attached at IRQ2 */
    outb(PIC2_DATA, 0x02); /* slave's cascade identity: it's on IRQ2 */

    /* ICW4: 8086/88 mode */
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    /* Mask everything except IRQ0 (timer), IRQ1 (keyboard), and IRQ2
     * (the cascade line itself, must stay unmasked on the master or the
     * slave's interrupts can never get through). Nothing behind the
     * slave is wired up yet, so mask it entirely. */
    outb(PIC1_DATA, 0xF8); /* 1111 1000 */
    outb(PIC2_DATA, 0xFF); /* 1111 1111 */
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}
