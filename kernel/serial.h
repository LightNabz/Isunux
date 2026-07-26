#pragma once
#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_print(const char *s);
void serial_print_hex(uint64_t val);
void serial_print_dec(uint64_t val);
