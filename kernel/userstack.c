#include "userstack.h"
#include "kutil.h"

#define AT_NULL   0
#define AT_PAGESZ 6

#define MAX_STACK_ARGS 8

uint64_t build_initial_stack(uint64_t hhdm_offset,
                              uint64_t stack_phys,
                              uint64_t stack_base_vaddr,
                              uint64_t stack_size,
                              int argc,
                              const char **argv) {
    if (argc > MAX_STACK_ARGS) argc = MAX_STACK_ARGS;

    /* We write through the HHDM mapping (kernel-side access to the same
     * physical pages), but every pointer we actually place into the
     * structure has to be the USER virtual address of that byte, not
     * the HHDM one -- ring 3 will dereference these under its own page
     * tables, which don't have HHDM mapped down in the low canonical
     * half at all. */
    uint8_t *hhdm_top = (uint8_t *)(hhdm_offset + stack_phys + stack_size);
    uint64_t user_top = stack_base_vaddr + stack_size;

    uint8_t *write_ptr = hhdm_top;
    uint64_t argv_user_ptrs[MAX_STACK_ARGS];

    /* place the argv strings themselves first, growing downward */
    for (int i = argc - 1; i >= 0; i--) {
        uint64_t len = k_strlen(argv[i]) + 1; /* include the null terminator */
        write_ptr -= len;
        for (uint64_t j = 0; j < len; j++) write_ptr[j] = argv[i][j];

        uint64_t offset_from_top = (uint64_t)(hhdm_top - write_ptr);
        argv_user_ptrs[i] = user_top - offset_from_top;
    }

    /* 8-align before the pointer arrays start */
    write_ptr = (uint8_t *)((uint64_t)write_ptr & ~(uint64_t)7);

    /* argc, argv[0..argc-1], NULL, envp (empty -> just NULL), then a
     * minimal auxv (one real entry + the AT_NULL terminator pair) */
    uint64_t total_u64 = 1 + (uint64_t)(argc + 1) + 1 + 4;
    uint64_t arr_start = (uint64_t)write_ptr - (total_u64 * 8);

    /* the ABI wants RSP 16-byte aligned at process entry -- insert one
     * padding qword if the natural layout lands us 8-off instead */
    if (arr_start % 16 != 0) arr_start -= 8;

    uint64_t *arr = (uint64_t *)arr_start;
    int idx = 0;
    arr[idx++] = (uint64_t)argc;
    for (int i = 0; i < argc; i++) arr[idx++] = argv_user_ptrs[i];
    arr[idx++] = 0; /* argv NULL terminator */
    arr[idx++] = 0; /* envp -- empty, just its own NULL terminator */
    arr[idx++] = AT_PAGESZ;
    arr[idx++] = 4096;
    arr[idx++] = AT_NULL;
    arr[idx++] = 0;

    uint64_t initial_rsp_offset_from_top = (uint64_t)hhdm_top - arr_start;
    return user_top - initial_rsp_offset_from_top;
}
