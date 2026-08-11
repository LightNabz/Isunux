#include "mini_libc.h"
#include "mini_printf.h"

/* Boot-verification for Tier 3 step 1b: proves all 6 registers of the
 * real x86_64 syscall ABI (rdi/rsi/rdx/r10/r8/r9) survive the full
 * round trip -- mini_libc's syscall6() -> syscall_entry's manual frame
 * build (isr.asm) -> kernel/syscall.c's SYS_ARGTEST case -- unmolested
 * and in the right slots. sys_argtest() (mini_libc.h) sends 6 fixed,
 * easy-to-eyeball sentinel values; the kernel echoes back one bit per
 * argument that matched what it expected. */
int main(void) {
    const char *names[6] = { "rdi", "rsi", "rdx", "r10", "r8", "r9" };

    long result = sys_argtest();

    if (result == 0x3f) {
        printf("PASS: all 6 registers arrived correctly (mask = 0x%x)\n", (int)result);
        return 0;
    }

    printf("FAIL: mask = 0x%x, expected 0x3f\n", (int)result);
    for (int i = 0; i < 6; i++) {
        if (!(result & (1 << i))) {
            printf("  %s did not arrive correctly\n", names[i]);
        }
    }
    return 1;
}
