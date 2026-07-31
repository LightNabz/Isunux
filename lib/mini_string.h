#pragma once
#include <stddef.h>

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int val, size_t n);
