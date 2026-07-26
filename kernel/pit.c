#include "pit.h"
#include "kutil.h"

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND        0x43
#define PIT_BASE_FREQ      1193182u

void pit_init(uint32_t freq_hz) {
    uint32_t divisor = PIT_BASE_FREQ / freq_hz;

    /* channel 0, access mode lobyte/hibyte, mode 3 (square wave) */
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
}
