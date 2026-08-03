#include "mini_libc.h"
#include "mini_printf.h"
#include <stdint.h>

#define MAP_SIZE (3 * 4096) /* spans multiple pages -- exercises the per-page loop in process_mmap(), not just a single-page edge case */

int main(void) {
    uint8_t *a = (uint8_t *)sys_mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS);
    if (!a) {
        printf("mmaptest: first mmap failed\n");
        return 1;
    }

    /* real mmap() guarantees zero-initialized pages */
    int zero_ok = 1;
    for (int i = 0; i < MAP_SIZE; i++) {
        if (a[i] != 0) { zero_ok = 0; break; }
    }
    printf("zero-init check: %s\n", zero_ok ? "PASS" : "FAIL");

    /* write a pattern spanning all three pages, verify it round-trips */
    for (int i = 0; i < MAP_SIZE; i++) {
        a[i] = (uint8_t)(i % 251);
    }
    int content_ok = 1;
    int mismatch_at = -1;
    for (int i = 0; i < MAP_SIZE; i++) {
        if (a[i] != (uint8_t)(i % 251)) { content_ok = 0; mismatch_at = i; break; }
    }
    printf("content check: %s%s\n", content_ok ? "PASS" : "FAIL",
           content_ok ? "" : " (mismatch)");
    if (!content_ok) printf("  mismatch at offset %d\n", mismatch_at);

    /* a second mmap should land at a distinct, higher address -- the
     * arena only grows, it doesn't reuse munmap()'d space */
    uint8_t *b = (uint8_t *)sys_mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS);
    int distinct_ok = (b != 0 && (uint64_t)b > (uint64_t)a);
    printf("second mapping at a distinct higher address: %s\n", distinct_ok ? "PASS" : "FAIL");

    if (sys_munmap((unsigned long)a, MAP_SIZE) != 0) {
        printf("mmaptest: munmap failed\n");
        return 1;
    }
    printf("munmap: PASS (call succeeded)\n");

    /* fork after mmap-ing a region -- confirms it participates in the
     * existing COW machinery with no special-casing needed, same
     * pattern as cowtest but against an mmap'd page instead of .data */
    int fork_ok = 1;
    long pid = sys_fork();
    if (pid == 0) {
        b[0] = 99; /* child's private copy, via COW fault -- should never reach the parent's page */
        sys_exit(0);
    }
    int status = 0;
    sys_waitpid((int)pid, &status);
    if (b[0] != 0) {
        fork_ok = 0;
    }
    printf("mmap region survives fork with COW isolation: %s\n", fork_ok ? "PASS" : "FAIL");

    if (zero_ok && content_ok && distinct_ok && fork_ok) {
        printf("PASS: all mmap checks passed\n");
        return 0;
    }
    printf("FAIL: at least one mmap check above did not pass\n");
    return 1;
}
