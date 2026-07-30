#include "fb.h"
#include "kutil.h"
#include "font8x16.h"

#define GLYPH_W 8
#define GLYPH_H 16

/* classic light-gray-on-black console colors */
#define FG_R 0xAA
#define FG_G 0xAA
#define FG_B 0xAA
#define BG_R 0x00
#define BG_G 0x00
#define BG_B 0x00

static struct limine_framebuffer *fb = NULL;
static bool inited = false;

static uint32_t cols = 0;
static uint32_t rows = 0;
static uint32_t cursor_col = 0;
static uint32_t cursor_row = 0;

static uint32_t fg_color = 0;
static uint32_t bg_color = 0;

/* scale an 8-bit channel value down to this field's mask width, then
 * shift it into place -- e.g. a 5-bit red field only keeps the top 5
 * bits of the 8-bit input. */
static inline uint32_t pack_channel(uint8_t val8, uint8_t mask_size, uint8_t shift) {
    if (mask_size == 0) return 0;
    uint32_t v = mask_size >= 8 ? val8 : (val8 >> (8 - mask_size));
    return v << shift;
}

static inline uint32_t pack_color(uint8_t r, uint8_t g, uint8_t b) {
    return pack_channel(r, fb->red_mask_size, fb->red_mask_shift)
         | pack_channel(g, fb->green_mask_size, fb->green_mask_shift)
         | pack_channel(b, fb->blue_mask_size, fb->blue_mask_shift);
}

static inline void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    uint32_t bytes_per_px = fb->bpp / 8;
    uint8_t *dst = (uint8_t *)fb->address + (uint64_t)y * fb->pitch + (uint64_t)x * bytes_per_px;
    for (uint32_t i = 0; i < bytes_per_px; i++) {
        dst[i] = (uint8_t)(color >> (8 * i));
    }
}

static void fb_fill_rows(uint32_t y_start, uint32_t y_end, uint32_t color) {
    for (uint32_t y = y_start; y < y_end; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            fb_putpixel(x, y, color);
        }
    }
}

static void fb_draw_glyph(uint32_t px, uint32_t py, char c) {
    static const uint8_t blank_glyph[GLYPH_H] = { 0 };
    const uint8_t *glyph;
    uint8_t uc = (uint8_t)c;

    if (uc >= FONT8X16_FIRST && uc <= FONT8X16_LAST) {
        glyph = font8x16[uc - FONT8X16_FIRST];
    } else {
        glyph = blank_glyph;
    }

    for (uint32_t row = 0; row < GLYPH_H; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < GLYPH_W; col++) {
            bool set = bits & (1 << (7 - col));
            fb_putpixel(px + col, py + row, set ? fg_color : bg_color);
        }
    }
}

static void fb_scroll_one_line(void) {
    uint64_t row_bytes   = (uint64_t)fb->pitch * GLYPH_H;
    uint64_t total_bytes = (uint64_t)fb->pitch * fb->height;

    /* shift the whole framebuffer up by one text row. dst < src for
     * this copy, so a plain forward byte-by-byte copy is safe even
     * though the regions overlap -- every byte is read before the
     * (lower) address it gets written to is ever touched again. */
    k_memcpy((uint8_t *)fb->address,
             (uint8_t *)fb->address + row_bytes,
             total_bytes - row_bytes);

    fb_fill_rows(fb->height - GLYPH_H, fb->height, bg_color);
}

void fb_init(struct limine_framebuffer_response *response) {
    if (response == NULL || response->framebuffer_count < 1) return;

    fb = response->framebuffers[0];
    if (fb == NULL || fb->address == NULL) { fb = NULL; return; }

    cols = (uint32_t)(fb->width / GLYPH_W);
    rows = (uint32_t)(fb->height / GLYPH_H);
    cursor_col = 0;
    cursor_row = 0;

    fg_color = pack_color(FG_R, FG_G, FG_B);
    bg_color = pack_color(BG_R, BG_G, BG_B);

    fb_fill_rows(0, (uint32_t)fb->height, bg_color);

    inited = true;
}

bool fb_available(void) {
    return inited;
}

static void fb_newline(void) {
    cursor_col = 0;
    if (cursor_row + 1 >= rows) {
        fb_scroll_one_line();
    } else {
        cursor_row++;
    }
}

void fb_putc(char c) {
    if (!inited) return;

    if (c == '\n') {
        fb_newline();
        return;
    }
    if (c == '\r') {
        cursor_col = 0;
        return;
    }
    if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = cols - 1;
        }
        fb_draw_glyph(cursor_col * GLYPH_W, cursor_row * GLYPH_H, ' ');
        return;
    }

    fb_draw_glyph(cursor_col * GLYPH_W, cursor_row * GLYPH_H, c);
    cursor_col++;
    if (cursor_col >= cols) {
        fb_newline();
    }
}

void fb_print(const char *s) {
    if (!inited) return;
    for (size_t i = 0; s[i] != '\0'; i++) {
        fb_putc(s[i]);
    }
}
