#include "mini_libc.h"
#include "mini_printf.h"
#include <stdint.h>

/* Exercises pipe.c's conversion from busy-yield polling to real
 * task_block()/task_wake() blocking. TOTAL_BYTES is well past
 * PIPE_BUF_SIZE (4096 in pipe.c), so the writer WILL block on a full
 * buffer and the reader WILL block on an empty one at various points --
 * this is exactly the scenario that would deadlock if pipe_write()
 * didn't wake a waiting reader before blocking itself (see the comment
 * in pipe_write()). CHUNK is deliberately not a power of two and
 * doesn't evenly divide TOTAL_BYTES, same reasoning as tmpfstest: it
 * forces writes that straddle the buffer-full boundary instead of
 * neatly lining up with it. */
#define TOTAL_BYTES (20 * 1024)
#define CHUNK       137

int main(void) {
    int fds[2];
    if (sys_pipe(fds) < 0) {
        printf("pipetest: sys_pipe failed\n");
        return 1;
    }
    int read_end = fds[0];
    int write_end = fds[1];

    long pid = sys_fork();
    if (pid < 0) {
        printf("pipetest: fork failed\n");
        return 1;
    }

    if (pid == 0) {
        /* child: writer. Close the end we don't use immediately --
         * standard pipe idiom, and load-bearing here: the parent's EOF
         * depends on write_refcount actually reaching 0, which needs
         * every write-end fd closed, not just this one. */
        sys_close(read_end);

        uint8_t chunk[CHUNK];
        uint64_t written = 0;
        while (written < TOTAL_BYTES) {
            uint64_t this_chunk = TOTAL_BYTES - written;
            if (this_chunk > CHUNK) this_chunk = CHUNK;
            for (uint64_t i = 0; i < this_chunk; i++) {
                chunk[i] = (uint8_t)((written + i) % 251);
            }
            long n = sys_write(write_end, chunk, this_chunk);
            if (n <= 0) {
                printf("[child] pipetest: write failed at offset %d\n", (int)written);
                sys_exit(1);
            }
            written += (uint64_t)n;
        }
        sys_close(write_end);
        printf("[child] wrote %d bytes, closed write end\n", (int)written);
        sys_exit(0);
    }

    /* parent: reader. Close the end we don't use -- same reasoning as
     * the child above, mirrored. */
    sys_close(write_end);

    uint8_t chunk[CHUNK];
    uint64_t checked = 0;
    int mismatch_at = -1;
    for (;;) {
        long n = sys_read(read_end, chunk, CHUNK);
        if (n < 0) {
            printf("[parent] pipetest: read error at offset %d\n", (int)checked);
            return 1;
        }
        if (n == 0) break; /* real EOF -- write_refcount hit 0 */

        for (long i = 0; i < n; i++) {
            uint8_t expected = (uint8_t)((checked + (uint64_t)i) % 251);
            if (chunk[i] != expected && mismatch_at < 0) {
                mismatch_at = (int)(checked + (uint64_t)i);
            }
        }
        checked += (uint64_t)n;
    }
    sys_close(read_end);

    int status = 0;
    sys_waitpid((int)pid, &status);

    printf("[parent] read back %d bytes (expected %d)\n", (int)checked, TOTAL_BYTES);

    if (checked != TOTAL_BYTES) {
        printf("FAIL: byte count mismatch -- either a lost wakeup or a premature EOF\n");
        return 1;
    }
    if (mismatch_at >= 0) {
        printf("FAIL: content mismatch at byte offset %d\n", mismatch_at);
        return 1;
    }

    printf("PASS: %d bytes written and read back byte-for-byte correct, no deadlock, clean EOF\n", TOTAL_BYTES);
    return 0;
}
