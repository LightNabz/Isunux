#include "mini_libc.h"
#include "mini_printf.h"
#include <stdint.h>

/* Boot-verification for Tier 3 step 1c. Two things to prove:
 *   1. envp actually arrived and getenv() can read it -- run this
 *      after "FOO=bar" at the shell prompt and confirm it shows up.
 *   2. the auxv build_initial_stack() (userstack.c) wrote is real,
 *      well-formed data, not garbage -- read directly here the exact
 *      same way a real musl/glibc _start would: walk past envp's own
 *      NULL terminator to find it, since nothing hands auxv to main()
 *      as a parameter, real _start's don't get it that way either. */

#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_ENTRY  9
#define AT_RANDOM 25

int main(int argc, char **argv, char **envp) {
    printf("--- argv (%d) ---\n", argc);
    for (int i = 0; i < argc; i++) printf("  argv[%d] = %s\n", i, argv[i]);

    printf("--- envp ---\n");
    int envc = 0;
    for (; envp && envp[envc]; envc++) printf("  %s\n", envp[envc]);

    printf("--- getenv ---\n");
    char *foo = getenv("FOO");
    printf("  getenv(\"FOO\") = %s\n", foo ? foo : "(not set)");

    /* auxv sits right after envp's NULL terminator -- same "walk past
     * the array" trick used to find envp itself in crt0.asm, one level
     * further out */
    uint64_t *auxv = (uint64_t *)(envp + envc + 1);

    printf("--- auxv ---\n");
    uint64_t pagesz = 0, entry = 0, phdr = 0, phent = 0, phnum = 0, random_addr = 0;
    int saw_pagesz = 0, saw_entry = 0, saw_phdr = 0, saw_random = 0;
    for (int i = 0; ; i += 2) {
        uint64_t type = auxv[i];
        uint64_t val = auxv[i + 1];
        if (type == AT_NULL) break;
        switch (type) {
            case AT_PAGESZ: pagesz = val; saw_pagesz = 1; break;
            case AT_ENTRY:  entry = val;  saw_entry = 1;  break;
            case AT_PHDR:   phdr = val;   saw_phdr = 1;   break;
            case AT_PHENT:  phent = val; break;
            case AT_PHNUM:  phnum = val; break;
            case AT_RANDOM: random_addr = val; saw_random = 1; break;
            default: break;
        }
    }

    printf("  AT_PAGESZ = %d (expect 4096)\n", (int)pagesz);
    printf("  AT_ENTRY  = 0x%x\n", (int)entry);
    printf("  AT_PHDR   = 0x%x\n", (int)phdr);
    printf("  AT_PHENT  = %d (expect 56)\n", (int)phent);
    printf("  AT_PHNUM  = %d\n", (int)phnum);
    printf("  AT_RANDOM = 0x%x -> ", (int)random_addr);
    if (random_addr) {
        unsigned char *bytes = (unsigned char *)random_addr;
        for (int i = 0; i < 16; i++) printf("%x ", bytes[i]);
    }
    printf("\n");

    int ok = saw_pagesz && pagesz == 4096 && saw_entry && entry != 0 &&
             saw_phdr && phdr != 0 && phent == 56 && phnum != 0 && saw_random;

    printf(ok ? "PASS: auxv is fully populated and looks sane\n"
              : "FAIL: something in the auxv is missing or wrong, see above\n");
    return ok ? 0 : 1;
}
