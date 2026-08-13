#include "userstack.h"
#include "kutil.h"

#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_ENTRY  9
#define AT_RANDOM 25

#define MAX_STACK_ARGS 8
#define MAX_STACK_ENVS 16
#define AUXV_ENTRIES 6 /* AT_PHDR, AT_PHENT, AT_PHNUM, AT_PAGESZ, AT_ENTRY, AT_RANDOM -- NOT counting the AT_NULL terminator pair */

/* Not cryptographically secure -- there's no real entropy source in
 * this kernel yet (no RDRAND use, no interrupt-timing pool, nothing).
 * All AT_RANDOM actually needs to be for glibc/musl's _start to work
 * is NON-CONSTANT bytes that exist at a valid address; it's only ever
 * used for the stack-protector canary and a couple of ASLR decisions
 * we don't make anyway (this loader has no ASLR). RDTSC-seeded
 * xorshift64 is enough for that bar. A real entropy source is future
 * work (getrandom() territory, Tier 4+), not a gap this milestone
 * needs to close. */
static uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint64_t build_initial_stack(uint64_t hhdm_offset,
                              uint64_t stack_phys,
                              uint64_t stack_base_vaddr,
                              uint64_t stack_size,
                              int argc,
                              const char **argv,
                              int envc,
                              const char **envp,
                              uint64_t entry_point,
                              uint64_t phdr_vaddr,
                              uint16_t phentsize,
                              uint16_t phnum) {
    if (argc > MAX_STACK_ARGS) argc = MAX_STACK_ARGS;
    if (envc > MAX_STACK_ENVS) envc = MAX_STACK_ENVS;

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
    uint64_t envp_user_ptrs[MAX_STACK_ENVS];

    /* place the argv strings themselves first, growing downward */
    for (int i = argc - 1; i >= 0; i--) {
        uint64_t len = k_strlen(argv[i]) + 1; /* include the null terminator */
        write_ptr -= len;
        for (uint64_t j = 0; j < len; j++) write_ptr[j] = argv[i][j];

        uint64_t offset_from_top = (uint64_t)(hhdm_top - write_ptr);
        argv_user_ptrs[i] = user_top - offset_from_top;
    }

    /* then the envp strings, same technique, continuing to grow down
     * from wherever the argv strings left off */
    for (int i = envc - 1; i >= 0; i--) {
        uint64_t len = k_strlen(envp[i]) + 1;
        write_ptr -= len;
        for (uint64_t j = 0; j < len; j++) write_ptr[j] = envp[i][j];

        uint64_t offset_from_top = (uint64_t)(hhdm_top - write_ptr);
        envp_user_ptrs[i] = user_top - offset_from_top;
    }

    /* 16 raw bytes for AT_RANDOM -- doesn't need alignment of its own,
     * just an address, same as any other string data above */
    write_ptr -= 16;
    uint64_t rand_seed = rdtsc() ^ (uint64_t)(uintptr_t)argv;
    for (int i = 0; i < 16; i += 8) {
        uint64_t chunk = xorshift64(&rand_seed);
        for (int j = 0; j < 8; j++) write_ptr[i + j] = (uint8_t)(chunk >> (j * 8));
    }
    uint64_t at_random_offset_from_top = (uint64_t)(hhdm_top - write_ptr);
    uint64_t at_random_user_addr = user_top - at_random_offset_from_top;

    /* 8-align before the pointer arrays start */
    write_ptr = (uint8_t *)((uint64_t)write_ptr & ~(uint64_t)7);

    /* argc, argv[0..argc-1], NULL, envp[0..envc-1], NULL, then the
     * full auxv (AUXV_ENTRIES real pairs + the AT_NULL terminator pair) */
    uint64_t total_u64 = 1 + (uint64_t)(argc + 1) + (uint64_t)(envc + 1) + (AUXV_ENTRIES + 1) * 2;
    uint64_t arr_start = (uint64_t)write_ptr - (total_u64 * 8);

    /* the ABI wants RSP 16-byte aligned at process entry -- insert one
     * padding qword if the natural layout lands us 8-off instead */
    if (arr_start % 16 != 0) arr_start -= 8;

    uint64_t *arr = (uint64_t *)arr_start;
    int idx = 0;
    arr[idx++] = (uint64_t)argc;
    for (int i = 0; i < argc; i++) arr[idx++] = argv_user_ptrs[i];
    arr[idx++] = 0; /* argv NULL terminator */
    for (int i = 0; i < envc; i++) arr[idx++] = envp_user_ptrs[i];
    arr[idx++] = 0; /* envp NULL terminator */

    arr[idx++] = AT_PHDR;   arr[idx++] = phdr_vaddr;
    arr[idx++] = AT_PHENT;  arr[idx++] = phentsize;
    arr[idx++] = AT_PHNUM;  arr[idx++] = phnum;
    arr[idx++] = AT_PAGESZ; arr[idx++] = 4096;
    arr[idx++] = AT_ENTRY;  arr[idx++] = entry_point;
    arr[idx++] = AT_RANDOM; arr[idx++] = at_random_user_addr;
    arr[idx++] = AT_NULL;   arr[idx++] = 0;

    uint64_t initial_rsp_offset_from_top = (uint64_t)hhdm_top - arr_start;
    return user_top - initial_rsp_offset_from_top;
}
