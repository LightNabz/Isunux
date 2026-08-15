#pragma once
#include <stdint.h>
#include <stddef.h>

static inline void *k_memset(void *dst, int val, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)val;
    return dst;
}

static inline void *k_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Reads `count` 16-bit words from `port` into `addr`, back to back --
 * this is how a whole ATA PIO sector gets pulled in (256 words = 512
 * bytes) without an inw() call per word. `rep insw` is the x86
 * string-instruction form of exactly that loop, done in one
 * instruction. */
static inline void insw(uint16_t port, void *addr, uint32_t count) {
    asm volatile ("rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, const void *addr, uint32_t count) {
    asm volatile ("rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}

/* The classic "write a throwaway byte to port 0x80" delay trick --
 * 0x80 is the POST-diagnostic-code port, unused once boot is past
 * POST, so writing to it is a safe, guaranteed-uncached bus cycle that
 * costs roughly one I/O-bus turnaround (~1us on real hardware). ATA
 * PIO relies on this in a few places to satisfy the spec's 400ns
 * "let the status bits settle after selecting a drive" rule -- see
 * ata.c's ata_delay_400ns(). Named for what it does, not how; the
 * exact mechanism is a well-known OSDev-wiki idiom, not something
 * this project invented. */
static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    asm volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

#define MSR_FS_BASE 0xC0000100 /* per-task TLS pointer -- see arch_prctl(ARCH_SET_FS,...) in syscall.c, saved/restored in process_t::fs_base and reloaded on every context switch in task.c */

static inline uint64_t k_strlen(const char *s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

static inline int k_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
