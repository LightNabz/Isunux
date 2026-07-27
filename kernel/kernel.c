#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "serial.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "pmm.h"
#include "vmm.h"
#include "task.h"
#include "tss.h"
#include "syscall.h"
#include "usermode.h"
#include "kutil.h"
#include "vfs.h"
#include "process.h"

__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".requests")))
static volatile struct limine_kernel_address_request kaddr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
};

static void hcf(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

/* the embedded flat binary from userprog/hello.asm, via userprog/hello_blob.asm */
extern uint8_t user_hello_start[];
extern uint8_t user_hello_end[];

void _start(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    serial_init();
    serial_print("========================================\n");
    serial_print(" milestone 7, group a: vfs + tmpfs +\n");
    serial_print(" processes/fds + real syscalls\n");
    serial_print("========================================\n");

    gdt_init();
    pic_remap();
    idt_init();
    pit_init(100);
    asm volatile ("sti");
    serial_print("[ok] gdt/idt/pic/pit up, interrupts enabled (user segments + tss from milestone 6)\n");

    if (memmap_request.response == NULL || hhdm_request.response == NULL ||
        kaddr_request.response == NULL) {
        serial_print("!!! limine didn't answer a request we need. halting.\n");
        hcf();
    }

    pmm_init(memmap_request.response);

    uint64_t hhdm_offset = hhdm_request.response->offset;
    uint64_t kernel_phys = kaddr_request.response->physical_base;
    uint64_t kernel_virt = kaddr_request.response->virtual_base;

    vmm_init(hhdm_offset, kernel_phys, kernel_virt);
    serial_print("\n[ok] pmm + vmm initialized, cr3 on our own kernel tables\n");

    vfs_init();
    serial_print("[ok] vfs initialized (tmpfs root, seeded /hello.txt)\n");

    serial_print("\n========================================\n");
    serial_print(" entering ring 3 to exercise real syscalls\n");
    serial_print("========================================\n");

    #define USER_CODE_VADDR   0x400000ULL
    #define USER_STACK_TOP    0x600000ULL
    #define USER_STACK_PAGES  4

    /* fresh address space for this "process" -- separate from the one
     * we built and tore down in the step 1+2 test above */
    uint64_t proc_as = vmm_new_address_space();
    serial_print("[ok] new address space for the test process: ");
    serial_print_hex(proc_as);
    serial_print("\n");

    /* copy the embedded flat binary (built from userprog/hello.asm) into
     * freshly allocated pages, then map those pages at the exact
     * virtual address it was assembled to run at */
    uint64_t prog_size = (uint64_t)(user_hello_end - user_hello_start);
    uint64_t prog_pages = (prog_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t code_phys = pmm_alloc_pages(prog_pages);

    uint8_t *code_dst = (uint8_t *)(hhdm_offset + code_phys);
    k_memset(code_dst, 0, prog_pages * PAGE_SIZE);
    for (uint64_t i = 0; i < prog_size; i++) {
        code_dst[i] = user_hello_start[i];
    }

    for (uint64_t p = 0; p < prog_pages; p++) {
        vmm_map_4k_in(proc_as, USER_CODE_VADDR + p * PAGE_SIZE,
                      code_phys + p * PAGE_SIZE, PTE_WRITE | PTE_USER);
    }
    serial_print("[ok] mapped ");
    serial_print_dec(prog_size);
    serial_print(" bytes of user code at ");
    serial_print_hex(USER_CODE_VADDR);
    serial_print(" (");
    serial_print_dec(prog_pages);
    serial_print(" page(s). writable for now -- this flat binary's code and\n");
    serial_print("     data share one page with no ELF sections to tell them apart;\n");
    serial_print("     real per-segment permissions arrive with the ELF loader.\n");

    /* user stack -- writable, grows down from USER_STACK_TOP */
    uint64_t stack_phys = pmm_alloc_pages(USER_STACK_PAGES);
    uint64_t stack_base_vaddr = USER_STACK_TOP - (USER_STACK_PAGES * PAGE_SIZE);
    for (uint64_t p = 0; p < USER_STACK_PAGES; p++) {
        vmm_map_4k_in(proc_as, stack_base_vaddr + p * PAGE_SIZE,
                      stack_phys + p * PAGE_SIZE, PTE_WRITE | PTE_USER);
    }
    serial_print("[ok] mapped a ");
    serial_print_dec(USER_STACK_PAGES * PAGE_SIZE / 1024);
    serial_print("KiB user stack below ");
    serial_print_hex(USER_STACK_TOP);
    serial_print("\n");

    /* a dedicated kernel stack for handling this process's syscalls and
     * interrupts -- TSS.rsp0 is what the CPU loads the instant this
     * process traps from ring 3 into ring 0 */
    uint64_t kstack_phys = pmm_alloc_pages(4);
    uint64_t kstack_top = hhdm_offset + kstack_phys + (4 * PAGE_SIZE);
    tss_set_kernel_stack(kstack_top);
    serial_print("[ok] tss.rsp0 set to a dedicated kernel stack for this process\n");

    static process_t test_process;
    process_init(&test_process, proc_as);
    current_process = &test_process;
    serial_print("[ok] process_t set up: fd 0/1/2 = console (stdin/stdout/stderr)\n");

    vmm_activate(proc_as);
    serial_print("[ok] cr3 switched to the process's address space\n");

    serial_print("\nentering ring 3 now. if this works, the next line of\n");
    serial_print("output comes from a syscall made by actual usermode code:\n\n");

    enter_userspace(USER_CODE_VADDR, USER_STACK_TOP);

    /* enter_userspace never returns -- if we somehow get here, something
     * is badly wrong */
    serial_print("!!! enter_userspace returned. this should be impossible.\n");
    hcf();
}
