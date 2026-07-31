#pragma once

/* %d, %s, %x, %c, %% only -- no floats, no width/precision specifiers.
 * Standard hobby-libc scope cut. Matches the real printf signature
 * (including returning int, the character count written) so GCC's
 * builtin-declaration checking has nothing to complain about. */
int printf(const char *fmt, ...);
