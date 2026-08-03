#include "idt.h"
#include "serial.h"
#include "pmm.h"
#include "vmm.h"
#include "process.h"
#include "task.h"
#include "syscall.h"
#include "kutil.h"

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

/* Error code bit layout for vector 14 (#PF), per the Intel SDM. */
#define PF_ERR_PRESENT (1ULL << 0) /* 0 = fault was a not-present page, 1 = a protection violation on a page that IS present */
#define PF_ERR_WRITE   (1ULL << 1) /* 0 = read access, 1 = write access */

/* Returns 1 if this fault was fully handled and it's safe to retry the
 * faulting instruction (the normal isr.asm epilogue does that
 * automatically just by returning and iretq-ing), 0 if it's a genuine
 * fault that should fall through to the fatal path below. */
static int try_handle_cow_fault(uint64_t cr2, uint64_t err_code) {
    /* only a write to an already-present page can possibly be a COW
     * fault -- anything else (not-present, or a read) is never one */
    if (!(err_code & PF_ERR_PRESENT) || !(err_code & PF_ERR_WRITE)) return 0;

    process_t *proc = process_current();
    if (!proc) return 0; /* fault happened with no current process (e.g. early boot) -- not a COW case */

    uint64_t *pte = vmm_get_pte(proc->pml4_phys, cr2);
    if (!pte) return 0; /* no mapping at all, or it's a 2MiB leaf -- not a COW case */
    if (!(*pte & PTE_COW)) return 0; /* present and writable-adjacent, but not one of ours -- a real protection violation */

    uint64_t phys = *pte & ~0xFFFULL;
    uint64_t page_vaddr = cr2 & ~0xFFFULL;

    if (pmm_page_refcount(phys) <= 1) {
        /* every other owner already dropped their reference (they wrote
         * their own copy first, or exited) -- we're the last one left,
         * so there's nothing left to protect against. Just reclaim this
         * page as sole-owned instead of copying it. */
        *pte = phys | PTE_PRESENT | PTE_WRITE | PTE_USER;
    } else {
        /* still actually shared -- make a private copy before letting
         * the write through, the entire point of copy-on-write */
        uint64_t new_phys = pmm_alloc_page();
        if (new_phys == 0) return 0; /* genuinely out of memory -- fall through to the fatal path, same as any other OOM in this kernel */

        k_memcpy((uint8_t *)(vmm_hhdm_offset() + new_phys), (uint8_t *)(vmm_hhdm_offset() + phys), PAGE_SIZE);
        *pte = new_phys | PTE_PRESENT | PTE_WRITE | PTE_USER;
        pmm_free_page(phys); /* drop OUR reference to the shared page -- doesn't free it while the other owner(s) still hold theirs */
    }

    vmm_invalidate_page(page_vaddr);
    return 1;
}

/* Ends the current process with exit_code and never returns -- same
 * "must not fall back into iretq-ing into userspace" requirement
 * sys_exit()/self-kill have, and for the same reason: our own task is
 * about to be TASK_TERMINATED. */
static void terminate_current_process(int exit_code) {
    process_t *proc = process_current();
    task_t *task = task_current();
    if (proc && task) {
        process_terminate(proc, task, exit_code);
    }
    for (;;) {
        yield();
    }
}

void exception_handler(interrupt_frame_t *frame) {
    if (frame->int_no == 14) {
        uint64_t cr2;
        asm volatile ("mov %%cr2, %0" : "=r"(cr2));

        if (try_handle_cow_fault(cr2, frame->err_code)) {
            return; /* isr.asm's epilogue pops registers and iretq's -- the faulting instruction just retries, now against a writable page */
        }
    }

    /* A genuine, unhandled fault that happened while running an actual
     * ring-3 process -- as opposed to a bug in the KERNEL's own code,
     * which still gets the full halt-and-inspect treatment below, since
     * that's a real reason to distrust the whole machine's state --
     * doesn't need to take the entire system down with it. Terminate
     * just that one process with SIGSEGV and let everything else keep
     * running, the way a real Unix kernel handles a user-mode segfault.
     * (frame->cs & 0x3) is the CPL the fault happened at: 3 means the
     * faulting code was running in ring 3. */
    if ((frame->cs & 0x3) == 0x3 && process_current()) {
        process_t *proc = process_current();
        serial_print("\n!!! unhandled fault in userspace (pid ");
        serial_print_dec((uint64_t)proc->pid);
        serial_print(") -- terminating with SIGSEGV instead of halting the kernel !!!\n");
        terminate_current_process(128 + SIGSEGV); /* never returns */
    }

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
        serial_print("  (either unmapped, or a genuine protection violation -- not a COW case, see err_code above)\n");
    }

    serial_print("the kernel caught this instead of triple faulting. halting now.\n");
    hcf();
}
