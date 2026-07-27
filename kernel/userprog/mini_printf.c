#include "mini_printf.h"
#include "mini_libc.h"
#include "mini_string.h"
#include <stdarg.h>
#include <stddef.h>

static void print_uint(char *buf, size_t *pos, size_t bufsize, unsigned long val, int base) {
    char tmp[32];
    int i = 0;
    const char *digits = "0123456789abcdef";

    if (val == 0) tmp[i++] = '0';
    while (val > 0) {
        tmp[i++] = digits[val % (unsigned long)base];
        val /= (unsigned long)base;
    }
    while (i > 0 && *pos < bufsize - 1) {
        buf[(*pos)++] = tmp[--i];
    }
}

static void print_int(char *buf, size_t *pos, size_t bufsize, long val) {
    if (val < 0) {
        if (*pos < bufsize - 1) buf[(*pos)++] = '-';
        print_uint(buf, pos, bufsize, (unsigned long)(-val), 10);
    } else {
        print_uint(buf, pos, bufsize, (unsigned long)val, 10);
    }
}

int printf(const char *fmt, ...) {
    char buf[256];
    size_t pos = 0;

    va_list ap;
    va_start(ap, fmt);

    for (size_t i = 0; fmt[i] && pos < sizeof(buf) - 1; i++) {
        if (fmt[i] != '%') {
            buf[pos++] = fmt[i];
            continue;
        }

        i++;
        if (!fmt[i]) break;

        switch (fmt[i]) {
            case 'd': {
                int v = va_arg(ap, int);
                print_int(buf, &pos, sizeof(buf), v);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                size_t len = strlen(s);
                for (size_t j = 0; j < len && pos < sizeof(buf) - 1; j++) buf[pos++] = s[j];
                break;
            }
            case 'x': {
                unsigned int v = va_arg(ap, unsigned int);
                print_uint(buf, &pos, sizeof(buf), v, 16);
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                buf[pos++] = c;
                break;
            }
            case '%': {
                buf[pos++] = '%';
                break;
            }
            default: {
                buf[pos++] = '%';
                if (pos < sizeof(buf) - 1) buf[pos++] = fmt[i];
                break;
            }
        }
    }

    va_end(ap);
    sys_write(1, buf, pos);
    return (int)pos;
}
