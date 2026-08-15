#include "mini_libc.h"
#include "mini_printf.h"
#include <stdint.h>

/* Boot-verification for Tier 3 step 1d. Two separate things to prove:
 *   1. arch_prctl(ARCH_SET_FS, ...) actually moves the real hardware
 *      FS.base, not just a bookkeeping field -- checked by dereferencing
 *      %fs:0 directly with inline asm, not by trusting ARCH_GET_FS
 *      (which would happily "confirm" a bug that only updated the
 *      process_t and never touched the MSR).
 *   2. it survives a CONTEXT SWITCH correctly -- this is the part
 *      task.c's per-switch MSR reload exists for. Fork gives the
 *      parent and child genuinely different FS.base values on purpose,
 *      then both spin for a while re-checking their own value on every
 *      iteration -- if the reload were missing (or only reloaded
 *      lazily/conditionally), the parent would eventually observe the
 *      child's value after being switched back into, or vice versa. */

static uint64_t parent_block[1] = { 0xAAAAAAAAAAAAAAAAULL };
static uint64_t child_block[1]  = { 0xBBBBBBBBBBBBBBBBULL };

static uint64_t read_fs0(void) {
    uint64_t val;
    asm volatile ("mov %%fs:0, %0" : "=r"(val));
    return val;
}

int main(void) {
    sys_arch_prctl(ARCH_SET_FS, (unsigned long)parent_block);

    unsigned long readback = 0;
    sys_arch_prctl(ARCH_GET_FS, (unsigned long)&readback);
    uint64_t direct = read_fs0();

    int basic_ok = (readback == (unsigned long)parent_block) && (direct == parent_block[0]);
    printf("single-task set/get/deref: %s (readback=0x%x direct=0x%x)\n",
           basic_ok ? "PASS" : "FAIL", (int)readback, (int)direct);

    long pid = sys_fork();
    if (pid == 0) {
        sys_arch_prctl(ARCH_SET_FS, (unsigned long)child_block);
        int ok = 1;
        for (int i = 0; i < 20; i++) {
            if (read_fs0() != child_block[0]) ok = 0;
            for (volatile int j = 0; j < 500000; j++); /* burn time -- give the scheduler room to switch us out mid-loop */
        }
        printf("[child]  fs.base held steady across the run: %s\n", ok ? "PASS" : "FAIL");
        sys_exit(ok ? 0 : 1);
    }

    int ok = 1;
    for (int i = 0; i < 20; i++) {
        if (read_fs0() != parent_block[0]) ok = 0;
        for (volatile int j = 0; j < 500000; j++);
    }
    printf("[parent] fs.base held steady across the run: %s\n", ok ? "PASS" : "FAIL");

    int status = 0;
    sys_waitpid((int)pid, &status);
    int child_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;

    int all_ok = basic_ok && ok && child_ok;
    printf(all_ok ? "PASS: fs_base survives set/get/deref and never bled across parent/child\n"
                  : "FAIL: see above\n");
    return all_ok ? 0 : 1;
}
