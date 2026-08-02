#include "mini_libc.h"
#include "mini_printf.h"
#include <stdint.h>

/* Throwaway test, not a real coreutil -- exercises tmpfs_file_write()'s
 * new grow-on-demand path directly, with no keyboard/paste involved.
 * Writes TOTAL_BYTES in small CHUNK-sized calls (so the file crosses
 * several capacity doublings starting from the 1-page initial size),
 * using a byte pattern that's cheap to verify on the way back out:
 * byte i is always (i % 251). 251 is prime and > 0 mod 256, so the
 * pattern doesn't alias with itself the way i % 256 would wrap into
 * looking identical to a truncated read at any multiple of 256. */
#define TOTAL_BYTES (20 * 1024) /* 20 KiB -- crosses 4K -> 8K -> 16K -> 32K */
#define CHUNK       137          /* deliberately not a power of two, and
                                   * doesn't evenly divide TOTAL_BYTES --
                                   * exercises the "grow mid-chunk" case
                                   * where a single write() straddles the
                                   * capacity boundary */

int main(void) {
    const char *path = "/tmp/tmpfstest.bin";

    sys_unlink(path); /* best-effort, fine if it doesn't exist yet */
    if (sys_create(path) < 0) {
        printf("tmpfstest: create failed\n");
        return 1;
    }

    long wfd = sys_open(path);
    if (wfd < 0) {
        printf("tmpfstest: open for write failed\n");
        return 1;
    }

    uint8_t chunk[CHUNK];
    uint64_t written = 0;
    while (written < TOTAL_BYTES) {
        uint64_t this_chunk = TOTAL_BYTES - written;
        if (this_chunk > CHUNK) this_chunk = CHUNK;

        for (uint64_t i = 0; i < this_chunk; i++) {
            chunk[i] = (uint8_t)((written + i) % 251);
        }

        long n = sys_write((int)wfd, chunk, this_chunk);
        if (n != (long)this_chunk) {
            printf("tmpfstest: short/failed write at offset %d (wrote %d, wanted %d)\n",
                   (int)written, (int)n, (int)this_chunk);
            sys_close((int)wfd);
            return 1;
        }
        written += (uint64_t)n;
    }
    sys_close((int)wfd);
    printf("wrote %d bytes\n", (int)written);

    stat_t st;
    if (sys_stat(path, &st) < 0) {
        printf("tmpfstest: stat failed\n");
        return 1;
    }
    printf("stat reports size %d (expected %d) -- %s\n",
           (int)st.size, TOTAL_BYTES, st.size == TOTAL_BYTES ? "MATCH" : "MISMATCH");

    long rfd = sys_open(path);
    if (rfd < 0) {
        printf("tmpfstest: open for read failed\n");
        return 1;
    }

    uint64_t checked = 0;
    int mismatch_at = -1;
    for (;;) {
        long n = sys_read((int)rfd, chunk, CHUNK);
        if (n < 0) {
            printf("tmpfstest: read error at offset %d\n", (int)checked);
            sys_close((int)rfd);
            return 1;
        }
        if (n == 0) break; /* EOF */

        for (long i = 0; i < n; i++) {
            uint8_t expected = (uint8_t)((checked + (uint64_t)i) % 251);
            if (chunk[i] != expected && mismatch_at < 0) {
                mismatch_at = (int)(checked + (uint64_t)i);
            }
        }
        checked += (uint64_t)n;
    }
    sys_close((int)rfd);

    printf("read back %d bytes (expected %d)\n", (int)checked, TOTAL_BYTES);

    if (checked != TOTAL_BYTES) {
        printf("FAIL: byte count mismatch -- this is exactly the silent-truncation bug we fixed, if it's back\n");
        return 1;
    }
    if (mismatch_at >= 0) {
        printf("FAIL: content mismatch at byte offset %d\n", mismatch_at);
        return 1;
    }

    printf("PASS: %d bytes written, stored, and read back byte-for-byte correct\n", TOTAL_BYTES);
    return 0;
}
