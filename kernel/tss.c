#include "tss.h"
#include "kutil.h"

tss_t kernel_tss;

void tss_init(void) {
    k_memset(&kernel_tss, 0, sizeof(kernel_tss));
    /* iomap_base == sizeof(tss_t) means "past the end of the segment
     * limit" -- there's no I/O permission bitmap at all, so every port
     * access from ring 3 gets rejected. We'll deal with letting
     * anything through deliberately later if we ever need to. */
    kernel_tss.iomap_base = sizeof(tss_t);
}

void tss_set_kernel_stack(uint64_t rsp0) {
    kernel_tss.rsp0 = rsp0;
}
