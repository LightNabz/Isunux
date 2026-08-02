#include "exec.h"
#include "process.h"
#include "vfs.h"
#include "vmm.h"
#include "pmm.h"
#include "elf.h"
#include "userstack.h"
#include "gdt.h"
#include "kutil.h"
#include "serial.h"

#define EXEC_BUF_SIZE   65536
#define MAX_EXEC_ARGS   8
#define EXEC_ARG_MAXLEN 64

/* Static kernel-side scratch space -- no kernel heap allocator exists
 * yet, and a one-shot exec() doesn't need one; a fixed buffer sized
 * well past any of our test binaries is simplest and correct. */
static uint8_t exec_buf[EXEC_BUF_SIZE];
static char exec_argv_storage[MAX_EXEC_ARGS][EXEC_ARG_MAXLEN];

#define USER_STACK_TOP   0x600000ULL
#define USER_STACK_PAGES 4

int64_t do_exec(interrupt_frame_t *frame, const char *path, char **user_argv) {
    /* Read argv's actual STRING CONTENTS out of the caller's still-active
     * address space right now, before anything about it changes. Once we
     * switch CR3 below, the old user pointers stop meaning anything --
     * they pointed into mappings that may not even exist in the new
     * address space at all. */
    int argc = 0;
    const char *argv_for_stack[MAX_EXEC_ARGS];

    if (user_argv) {
        while (argc < MAX_EXEC_ARGS && user_argv[argc] != NULL) {
            const char *src = user_argv[argc];
            int j = 0;
            while (src[j] && j < EXEC_ARG_MAXLEN - 1) {
                exec_argv_storage[argc][j] = src[j];
                j++;
            }
            exec_argv_storage[argc][j] = '\0';
            argv_for_stack[argc] = exec_argv_storage[argc];
            argc++;
        }
    }

    /* resolve + read the new program while we're still safely running
     * under the OLD address space -- doesn't touch user memory at all,
     * this is pure VFS/tmpfs work */
    process_t *proc = process_current();
    if (!proc) return -1;

    vnode_t *node = vfs_resolve_path_cwd(proc->cwd, path);
    if (!node || !node->ops || !node->ops->read) {
        serial_print("[exec] path not found or not readable\n");
        return -1;
    }

    long n = node->ops->read(node, exec_buf, sizeof(exec_buf), 0);
    if (n <= 0) {
        serial_print("[exec] read failed or empty file\n");
        return -1;
    }

    uint64_t new_pml4 = vmm_new_address_space();
    uint64_t entry_point = 0;
    uint64_t heap_start = 0;
    if (!elf_load(new_pml4, exec_buf, (uint64_t)n, &entry_point, &heap_start)) {
        serial_print("[exec] elf_load failed -- old process image untouched\n");
        return -1;
    }

    uint64_t stack_phys = pmm_alloc_pages(USER_STACK_PAGES);
    if (stack_phys == 0) {
        serial_print("[exec] out of memory for the new stack\n");
        return -1;
    }
    uint64_t stack_base_vaddr = USER_STACK_TOP - (USER_STACK_PAGES * PAGE_SIZE);
    for (uint64_t p = 0; p < USER_STACK_PAGES; p++) {
        vmm_map_4k_in(new_pml4, stack_base_vaddr + p * PAGE_SIZE,
                      stack_phys + p * PAGE_SIZE, PTE_WRITE | PTE_USER);
    }

    uint64_t initial_rsp = build_initial_stack(
        vmm_hhdm_offset(), stack_phys, stack_base_vaddr,
        USER_STACK_PAGES * PAGE_SIZE, argc, argv_for_stack);

    /* everything that could still fail has already happened -- past
     * this point we commit. Same process, same pid, same fd table
     * (real execve() preserves open files across an exec), but an
     * entirely fresh address space and a fresh heap. */
    uint64_t old_pml4 = proc->pml4_phys;
    proc->pml4_phys = new_pml4;
    proc->heap_start = heap_start;
    proc->heap_end = heap_start;

    vmm_activate(new_pml4);

    /* old_pml4 is safe to tear down now -- CR3 no longer points at it,
     * and it was never shared with anyone (fork() gives every child its
     * own copy, exec() never shares address spaces). Without this, every
     * fork()+exec() pair -- the standard shell pattern -- would leak a
     * full address space, not just a process that never gets reaped. */
    vmm_destroy_address_space(old_pml4);

    /* overwrite the CURRENT trap frame in place -- isr128's normal
     * epilogue (pop registers, iretq) runs right after syscall_handler
     * returns regardless, so this is the entire mechanism. General
     * registers zeroed for cleanliness, matching how a freshly started
     * process shouldn't see leftover values from whatever used to be
     * running in this memory. */
    frame->rax = 0;
    frame->rbx = 0;
    frame->rcx = 0;
    frame->rdx = 0;
    frame->rsi = 0;
    frame->rdi = 0;
    frame->rbp = 0;
    frame->r8 = 0;
    frame->r9 = 0;
    frame->r10 = 0;
    frame->r11 = 0;
    frame->r12 = 0;
    frame->r13 = 0;
    frame->r14 = 0;
    frame->r15 = 0;

    frame->rip = entry_point;
    frame->rsp = initial_rsp;
    frame->cs = GDT_USER_CODE;
    frame->ss = GDT_USER_DATA;
    frame->rflags |= 0x200; /* make sure IF is set, same as enter_userspace does for a fresh launch */

    return 0; /* never actually observed by the old program -- rip no longer points at it */
}
