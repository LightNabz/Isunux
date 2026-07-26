#include "idt.h"
#include "serial.h"

static const char *exception_names[32] = {
    "Divide-by-zero error", "Debug", "Non-maskable interrupt", "Breakpoint",
    "Overflow", "Bound range exceeded", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor segment overrun", "Invalid TSS", "Segment not present",
    "Stack-segment fault", "General protection fault", "Page fault", "Reserved",
    "x87 floating-point exception", "Alignment check", "Machine check", "SIMD floating-point exception",
    "Virtualization exception", "Control protection exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection exception", "VMM communication exception", "Security exception", "Reserved",
};

static void hcf(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void exception_handler(interrupt_frame_t *frame) {
    serial_print("\n!!! cpu exception !!!\n");

    serial_print("vector:      ");
    serial_print_dec(frame->int_no);
    serial_print("  (");
    serial_print(exception_names[frame->int_no]);
    serial_print(")\n");

    serial_print("error code:  ");
    serial_print_hex(frame->err_code);
    serial_print("\n");

    serial_print("rip:         ");
    serial_print_hex(frame->rip);
    serial_print("\n");

    serial_print("cs:          ");
    serial_print_hex(frame->cs);
    serial_print("\n");

    serial_print("rflags:      ");
    serial_print_hex(frame->rflags);
    serial_print("\n");

    if (frame->int_no == 14) { /* page fault -- CR2 holds the faulting address */
        uint64_t cr2;
        asm volatile ("mov %%cr2, %0" : "=r"(cr2));
        serial_print("fault addr:  ");
        serial_print_hex(cr2);
        serial_print("  (this is the virtual address that wasn't mapped)\n");
    }

    serial_print("the kernel caught this instead of triple faulting. halting now.\n");
    hcf();
}
