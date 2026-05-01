/*
 * vfd.h — header-only 5x7 dot-matrix VFD-style digit renderer.
 *
 * Extracted from psu/main.c so the native instrument tools (dmm_gui,
 * eload_gui) can use the same VFD look without linking a shared library
 * or modifying the imported PSU code. All functions are `static inline`,
 * so each translation unit gets its own copy and the tools stay
 * self-contained / standalone.
 *
 * Glyph set: 0-9, '.', '-', and space. Anything else is skipped.
 *
 * Usage:
 *   draw_vfd_number(renderer, x, y, "+12.345", dot_size, dot_gap, char_gap,
 *                   on_color, off_color, show_off_pixels);
 */
#ifndef APCOMMANDER_VFD_H
#define APCOMMANDER_VFD_H

#include <SDL2/SDL.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct VfdColor {
    Uint8 r, g, b, a;
} VfdColor;

/*
 * 5x7 dot-matrix patterns. Each glyph is 5 columns; each byte's low 7 bits
 * are that column (LSB = top row).
 */
static const uint8_t VFD_DOT_MATRIX_FONT[12][5] = {
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, /* 0 */
    { 0x00, 0x42, 0x7F, 0x40, 0x00 }, /* 1 */
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, /* 2 */
    { 0x21, 0x41, 0x45, 0x4B, 0x31 }, /* 3 */
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, /* 4 */
    { 0x27, 0x45, 0x45, 0x45, 0x39 }, /* 5 */
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, /* 6 */
    { 0x01, 0x71, 0x09, 0x05, 0x03 }, /* 7 */
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, /* 8 */
    { 0x06, 0x49, 0x49, 0x29, 0x1E }, /* 9 */
    { 0x00, 0x60, 0x60, 0x00, 0x00 }, /* . */
    { 0x08, 0x08, 0x08, 0x08, 0x08 }, /* - */
};

static inline int vfd_glyph_index(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c == '.')
        return 10;
    if (c == '-')
        return 11;
    return -1;
}

static inline void vfd_draw_dot(SDL_Renderer *r, int cx, int cy, int radius,
                                VfdColor col, bool on)
{
    int dy, dx;

    if (!on) {
        VfdColor dim = { (Uint8)(col.r / 15), (Uint8)(col.g / 15),
                         (Uint8)(col.b / 15), 80 };

        SDL_SetRenderDrawColor(r, dim.r, dim.g, dim.b, dim.a);
        for (dy = -radius + 1; dy < radius; dy++)
            for (dx = -radius + 1; dx < radius; dx++)
                if (dx * dx + dy * dy < radius * radius)
                    SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        return;
    }

    /* Outer glow */
    {
        int glow_r = radius + 2;
        VfdColor glow = { (Uint8)(col.r / 6), (Uint8)(col.g / 6),
                          (Uint8)(col.b / 6), 60 };

        SDL_SetRenderDrawColor(r, glow.r, glow.g, glow.b, glow.a);
        for (dy = -glow_r; dy <= glow_r; dy++)
            for (dx = -glow_r; dx <= glow_r; dx++) {
                int d2 = dx * dx + dy * dy;
                if (d2 <= glow_r * glow_r && d2 > radius * radius)
                    SDL_RenderDrawPoint(r, cx + dx, cy + dy);
            }
    }

    /* Body */
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    for (dy = -radius; dy <= radius; dy++)
        for (dx = -radius; dx <= radius; dx++)
            if (dx * dx + dy * dy <= radius * radius)
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);

    /* Bright center highlight */
    {
        VfdColor hi = { (Uint8)(col.r + (255 - col.r) / 2),
                        (Uint8)(col.g + (255 - col.g) / 2),
                        (Uint8)(col.b + (255 - col.b) / 2), 255 };
        int hi_r = radius / 2;

        if (hi_r < 1)
            hi_r = 1;
        SDL_SetRenderDrawColor(r, hi.r, hi.g, hi.b, hi.a);
        for (dy = -hi_r; dy <= hi_r; dy++)
            for (dx = -hi_r; dx <= hi_r; dx++)
                if (dx * dx + dy * dy <= hi_r * hi_r)
                    SDL_RenderDrawPoint(r, cx + dx - 1, cy + dy - 1);
    }
}

static inline void vfd_draw_char(SDL_Renderer *r, int x, int y, int ch_idx,
                                 int dot_size, int dot_gap, VfdColor on_col,
                                 VfdColor off_col, bool show_off)
{
    const uint8_t *pattern;
    int dot_spacing;
    int row, col;

    if (ch_idx < 0 || ch_idx > 11)
        return;
    pattern = VFD_DOT_MATRIX_FONT[ch_idx];
    dot_spacing = dot_size * 2 + dot_gap;

    for (col = 0; col < 5; col++) {
        uint8_t coldata = pattern[col];

        for (row = 0; row < 7; row++) {
            bool on = (coldata >> row) & 1;
            int px = x + col * dot_spacing + dot_size;
            int py = y + row * dot_spacing + dot_size;

            if (on)
                vfd_draw_dot(r, px, py, dot_size, on_col, true);
            else if (show_off)
                vfd_draw_dot(r, px, py, dot_size, off_col, false);
        }
    }
}

/*
 * Draws a number-like string ("+12.34", "-1.0", " 60") using the dot-matrix
 * font. Returns total pixel width drawn. Skips characters that aren't 0-9,
 * '.', '-', or ' '.
 */
static inline int vfd_draw_number(SDL_Renderer *r, int x, int y,
                                  const char *str, int dot_size, int dot_gap,
                                  int char_gap, VfdColor on_col,
                                  VfdColor off_col, bool show_off)
{
    int cx = x;
    int char_w = 5 * (dot_size * 2 + dot_gap);
    const char *p;

    if (!str)
        return 0;
    for (p = str; *p; p++) {
        int ch_idx = vfd_glyph_index(*p);

        if (*p == ' ') {
            cx += char_w + char_gap;
            continue;
        }
        if (ch_idx < 0)
            continue;
        vfd_draw_char(r, cx, y, ch_idx, dot_size, dot_gap, on_col, off_col,
                      show_off);
        if (ch_idx == 10) /* dot — narrower */
            cx += (dot_size * 2 + dot_gap) * 2 + char_gap;
        else
            cx += char_w + char_gap;
    }
    return cx - x;
}

/*
 * Picks the largest dot_size in [min_size, max_size] such that the given
 * `chars`-long number string fits in (avail_w × avail_h), assuming
 * dot_gap=1 and char_gap = 2 * dot_size. Width math:
 *
 *   per-glyph width  = 5 * (2*dot + 1)              = 10*dot + 5
 *   per-char-gap     = 2*dot
 *   total_w(N, dot)  = N*(10*dot + 5) + (N-1)*2*dot = (12N - 2)*dot + 5N
 *   total_h(dot)     = 7*(2*dot + 1)                = 14*dot + 7
 *
 * Use this before vfd_draw_number / vfd_measure so the readout always
 * fits the panel rather than overflowing.
 */
static inline int vfd_fit_dot_size(int chars, int avail_w, int avail_h,
                                   int min_size, int max_size)
{
    int dot_w;
    int dot_h;
    int dot;

    if (chars <= 0)
        return min_size;
    dot_w = (avail_w - 5 * chars) / (12 * chars - 2);
    dot_h = (avail_h - 7) / 14;
    dot = dot_w < dot_h ? dot_w : dot_h;
    if (dot < min_size)
        dot = min_size;
    if (dot > max_size)
        dot = max_size;
    return dot;
}

/*
 * Returns how wide a string would render at the given dot/gap settings.
 * Useful for right-aligning a readout in a panel.
 */
static inline int vfd_measure(const char *str, int dot_size, int dot_gap,
                              int char_gap)
{
    int w = 0;
    int char_w = 5 * (dot_size * 2 + dot_gap);
    const char *p;

    if (!str)
        return 0;
    for (p = str; *p; p++) {
        int ch_idx = vfd_glyph_index(*p);

        if (*p == ' ') {
            w += char_w + char_gap;
            continue;
        }
        if (ch_idx < 0)
            continue;
        if (ch_idx == 10)
            w += (dot_size * 2 + dot_gap) * 2 + char_gap;
        else
            w += char_w + char_gap;
    }
    return w;
}

#endif /* APCOMMANDER_VFD_H */
