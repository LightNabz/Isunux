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
#include "elf.h"
#include "userstack.h"
#include "fb.h"
#include "ata.h"

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

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
};

/* internal_module_count stays 0 -- that's what tells Limine "just give
 * me everything declared via module_path: in limine.conf", rather than
 * the kernel demanding specific paths by name. That's the whole point:
 * this file doesn't know or care what modules exist, see vfs_init(). */
__attribute__((used, section(".requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0,
    .internal_module_count = 0,
    .internal_modules = NULL,
};

static void hcf(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void _start(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    serial_init();

    /* set up the on-screen console as early as we can, so everything
     * printed from here on (via serial_print/serial_putc) shows up
     * both on the serial port and on screen. If Limine didn't hand us
     * a framebuffer, fb_putc()/fb_print() just stay no-ops. */
    if (framebuffer_request.response != NULL) {
        fb_init(framebuffer_request.response);
    }

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

    ata_init();
    if (ata_present()) {
        /* Round-trip smoke test: write a recognizable pattern to a
         * sector, read it back into a separate buffer, and compare --
         * this actually exercises both directions plus the write
         * path's cache-flush, unlike a bare read (which could
         * "succeed" against total garbage and look fine). Sector 100
         * is arbitrary but deliberately NOT sector 0 -- once the FAT
         * layer lands, sector 0 becomes the boot sector/BPB, and
         * scribbling test data there would be actively harmful to
         * keep around. This whole block is throwaway scaffolding for
         * this milestone only; it goes away once FAT has its own
         * tests to exercise the driver instead. */
        #define ATA_SELFTEST_LBA 100
        uint8_t write_buf[512];
        uint8_t read_buf[512];
        for (int i = 0; i < 512; i++) write_buf[i] = (uint8_t)(i * 3 + 7);

        int wrc = ata_write_sectors(ATA_SELFTEST_LBA, 1, write_buf);
        int rdc = wrc == 0 ? ata_read_sectors(ATA_SELFTEST_LBA, 1, read_buf) : -1;
        int match = 1;
        if (rdc == 0) {
            for (int i = 0; i < 512; i++) {
                if (write_buf[i] != read_buf[i]) { match = 0; break; }
            }
        }

        if (wrc == 0 && rdc == 0 && match) {
            serial_print("[ok] ata read/write self-test passed (sector round-trip verified)\n");
        } else {
            serial_print("!!! ata read/write self-test FAILED (wrc=");
            serial_print_dec((uint64_t)(wrc == 0 ? 0 : 1));
            serial_print(" rdc=");
            serial_print_dec((uint64_t)(rdc == 0 ? 0 : 1));
            serial_print(" match=");
            serial_print_dec((uint64_t)match);
            serial_print(") -- continuing boot anyway, nothing depends on disk yet\n");
        }
        #undef ATA_SELFTEST_LBA
    } else {
        serial_print("[ata] no disk attached this boot -- fine for now, nothing depends on it yet\n");
    }

    vfs_init((struct limine_module_response *)module_request.response);
    serial_print("[ok] vfs initialized (tmpfs root, seeded /hello.txt)\n");

    serial_print("\n========================================\n");
    serial_print(" milestone 9, group a: real process table\n");
    serial_print(" + scheduler integration + fork()\n");
    serial_print("========================================\n");

    #define USER_STACK_TOP    0x600000ULL
    #define USER_STACK_PAGES  4

    task_init();
    serial_print("[ok] task_init: current flow of execution is now 'main'\n");

    /* build the first real user process by loading /bin/sh through the
     * VFS -- the exact same mechanism exec() uses for every subsequent
     * program, so the very first process isn't a special case at all */
    uint64_t proc_as = vmm_new_address_space();

    vnode_t *sh_node = vfs_resolve_path("/bin/sh");
    if (!sh_node) {
        serial_print("!!! /bin/sh not found. halting.\n");
        hcf();
    }

    uint64_t sh_size = 0;
    uint64_t sh_buf_pages = 0;
    uint8_t *sh_buf = vfs_read_file_alloc(sh_node, &sh_size, &sh_buf_pages);
    if (!sh_buf || sh_size == 0) {
        serial_print("!!! failed to read /bin/sh. halting.\n");
        hcf();
    }

    uint64_t entry_point = 0;
    uint64_t heap_start = 0;
    int sh_ok = elf_load(proc_as, sh_buf, sh_size, &entry_point, &heap_start);
    vfs_read_file_free(sh_buf, sh_buf_pages); /* elf_load already copied every PT_LOAD segment into its own pages */
    if (!sh_ok) {
        serial_print("!!! elf_load failed. halting.\n");
        hcf();
    }

    uint64_t stack_phys = pmm_alloc_pages(USER_STACK_PAGES);
    uint64_t stack_base_vaddr = USER_STACK_TOP - (USER_STACK_PAGES * PAGE_SIZE);
    for (uint64_t p = 0; p < USER_STACK_PAGES; p++) {
        vmm_map_4k_in(proc_as, stack_base_vaddr + p * PAGE_SIZE,
                      stack_phys + p * PAGE_SIZE, PTE_WRITE | PTE_USER);
    }

    static const char *init_argv[] = { "sh" };
    uint64_t initial_rsp = build_initial_stack(
        hhdm_offset, stack_phys, stack_base_vaddr,
        USER_STACK_PAGES * PAGE_SIZE, 1, init_argv);

    process_t *proc = process_alloc(0); /* parent pid 0 = "the kernel" */
    if (!proc) {
        serial_print("!!! process_alloc failed. halting.\n");
        hcf();
    }
    process_init(proc, proc_as, heap_start);
    serial_print("[ok] process pid ");
    serial_print_dec((uint64_t)proc->pid);
    serial_print(" built: elf loaded, entry ");
    serial_print_hex(entry_point);
    serial_print(", heap starts at ");
    serial_print_hex(heap_start);
    serial_print("\n");

    task_t *proc_task = task_create_user("init", proc, entry_point, initial_rsp);
    if (!proc_task) {
        serial_print("!!! task_create_user failed. halting.\n");
        hcf();
    }
    serial_print("[ok] task created and scheduled -- not running yet, waiting its turn\n");

    serial_print("\nmain now just idles. the timer + scheduler take it from here.\n\n");

    /* main never touches ring 3 or calls enter_userspace itself anymore
     * -- it's just another task in the ring now. interrupts stay
     * enabled, so the timer keeps firing, keeps calling yield() from
     * inside irq_handler, and keeps preempting whichever task happens
     * to be running -- including the process we just created above. */
    for (;;) {
        asm volatile ("hlt");
    }
}
