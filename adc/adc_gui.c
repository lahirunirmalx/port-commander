/**
 * 24-bit ADC GUI — SDL2 with 8-channel display.
 * 
 * Features:
 * - 8 channels, individually enable/disable
 * - 8½ digit resolution (sign + 8 digits)
 * - Display scale: µV, mV, V
 * - Per-channel calibration: offset, gain, reference
 * - Raw ADC counts and scaled voltage display
 * - Statistics: min/max/avg
 * - Oscilloscope traces
 */

#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <sys/time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "serial_port.h"
#include "adc_protocol.h"

/* Window dimensions */
#define WIN_W 1500
#define WIN_H 920

/* Layout constants */
#define HEADER_H      40
#define TOOLBAR_H     36
#define SIDEBAR_W     380
#define CH_CARD_H     100
#define CH_CARD_GAP   4
#define SCOPE_MARGIN  10

/* Color palette */
typedef struct { Uint8 r, g, b, a; } Color;

static const Color COL_BG_DARK      = {25, 25, 28, 255};
static const Color COL_BG_PANEL     = {35, 35, 40, 255};
static const Color COL_BG_WIDGET    = {28, 28, 32, 255};
static const Color COL_HEADER       = {20, 20, 24, 255};
static const Color COL_BORDER       = {55, 55, 60, 255};
static const Color COL_BORDER_LIGHT = {75, 75, 82, 255};
static const Color COL_TEXT         = {200, 200, 205, 255};
static const Color COL_TEXT_DIM     = {110, 110, 118, 255};
static const Color COL_LABEL        = {150, 150, 158, 255};
static const Color COL_ACCENT       = {0, 160, 200, 255};
static const Color COL_SUCCESS      = {50, 200, 100, 255};
static const Color COL_WARNING      = {255, 180, 0, 255};
static const Color COL_ERROR        = {220, 60, 60, 255};
static const Color COL_VFD_BG __attribute__((unused)) = {6, 14, 10, 255};
static const Color COL_SCOPE_BG     = {8, 16, 12, 255};
static const Color COL_SCOPE_GRID   = {25, 45, 32, 255};
static const Color COL_BTN_NORMAL   = {50, 50, 55, 255};
static const Color COL_BTN_HOVER    = {65, 65, 72, 255};
static const Color COL_BTN_ACTIVE   = {0, 140, 170, 255};
static const Color COL_INPUT_BG __attribute__((unused)) = {20, 20, 24, 255};

/* Channel colors for traces */
static const Color CH_COLORS[8] = {
    {255, 100, 100, 255},  /* CH1 - Red */
    {100, 255, 100, 255},  /* CH2 - Green */
    {100, 150, 255, 255},  /* CH3 - Blue */
    {255, 255, 100, 255},  /* CH4 - Yellow */
    {255, 100, 255, 255},  /* CH5 - Magenta */
    {100, 255, 255, 255},  /* CH6 - Cyan */
    {255, 180, 100, 255},  /* CH7 - Orange */
    {180, 100, 255, 255},  /* CH8 - Purple */
};

/* VFD dot-matrix font (5x7) */
static const uint8_t DOT_MATRIX_FONT[14][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . (dot) */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - (minus) */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* + (plus) */
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
};

/* Button IDs */
#define BTN_SCALE_UV     1
#define BTN_SCALE_MV     2
#define BTN_SCALE_V      3
#define BTN_STATS_RESET  4
#define BTN_STATS_PAUSE  5
#define BTN_CH_BASE      100  /* CH1=100, CH2=101, etc. */
#define BTN_CAL_BASE     200  /* Calibration buttons */
#define MAX_BUTTONS      64

typedef struct {
    SDL_Rect rect;
    int id;
} button_t;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font_title;
    TTF_Font *font_large;
    TTF_Font *font_medium;
    TTF_Font *font_small;
    TTF_Font *font_mono;
    TTF_Font *font_mono_large;
    
    adc_context_t adc;
    adc_channel_t ch_cache[ADC_MAX_CHANNELS];  /* Local copy for rendering */
    
    bool running;
    bool demo_mode;
    int hover_btn;
    int selected_ch;  /* -1 = none, 0-7 = channel */
    
    /* Calibration edit mode */
    int cal_edit_ch;      /* -1 = not editing */
    int cal_edit_field;   /* 0=offset, 1=gain, 2=vref */
    char cal_input[32];
    
    uint32_t frame_count;
    uint32_t last_fps_time;
    int fps;
    
    button_t buttons[MAX_BUTTONS];
    int num_buttons;
} app_t;

static app_t g_app;

static uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* Drawing helpers */
static void set_color(SDL_Renderer *r, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

static void draw_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderDrawRect(r, &rect);
}

static void fill_rounded_rect(SDL_Renderer *r, int x, int y, int w, int h, int radius) {
    fill_rect(r, x + radius, y, w - 2 * radius, h);
    fill_rect(r, x, y + radius, w, h - 2 * radius);
    for (int corner = 0; corner < 4; corner++) {
        int cx = (corner % 2 == 0) ? x + radius : x + w - radius;
        int cy = (corner < 2) ? y + radius : y + h - radius;
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx * dx + dy * dy <= radius * radius) {
                    SDL_RenderDrawPoint(r, cx + dx, cy + dy);
                }
            }
        }
    }
}

static void draw_text(SDL_Renderer *r, TTF_Font *font, const char *text,
                      int x, int y, Color color, int align) {
    if (!font || !text || !*text) return;
    SDL_Color sdl_col = {color.r, color.g, color.b, color.a};
    SDL_Surface *surf = TTF_RenderText_Blended(font, text, sdl_col);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (tex) {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        if (align == 1) dst.x = x - surf->w / 2;
        else if (align == 2) dst.x = x - surf->w;
        SDL_RenderCopy(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

static void draw_text_centered(SDL_Renderer *r, TTF_Font *font, const char *text,
                               int cx, int cy, Color color) {
    if (!font || !text || !*text) return;
    SDL_Color sdl_col = {color.r, color.g, color.b, color.a};
    SDL_Surface *surf = TTF_RenderText_Blended(font, text, sdl_col);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (tex) {
        SDL_Rect dst = {cx - surf->w / 2, cy - surf->h / 2, surf->w, surf->h};
        SDL_RenderCopy(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

static int add_button(int x, int y, int w, int h, int id) {
    if (g_app.num_buttons >= MAX_BUTTONS) return -1;
    g_app.buttons[g_app.num_buttons] = (button_t){ .rect = {x, y, w, h}, .id = id };
    return g_app.num_buttons++;
}

static int button_at(int mx, int my) {
    for (int i = 0; i < g_app.num_buttons; i++) {
        SDL_Rect *r = &g_app.buttons[i].rect;
        if (mx >= r->x && mx < r->x + r->w && my >= r->y && my < r->y + r->h)
            return g_app.buttons[i].id;
    }
    return 0;
}

static void draw_button(SDL_Renderer *r, int x, int y, int w, int h,
                        const char *text, bool active, bool hover, int id) {
    Color bg = active ? COL_BTN_ACTIVE : (hover ? COL_BTN_HOVER : COL_BTN_NORMAL);
    Color border = active ? COL_ACCENT : COL_BORDER_LIGHT;
    Color text_col = active ? (Color){255, 255, 255, 255} : COL_TEXT;
    
    set_color(r, bg);
    fill_rounded_rect(r, x, y, w, h, 3);
    set_color(r, border);
    draw_rect(r, x, y, w, h);
    draw_text_centered(r, g_app.font_small, text, x + w / 2, y + h / 2, text_col);
    add_button(x, y, w, h, id);
}

static void draw_led(SDL_Renderer *r, int cx, int cy, int radius, bool on, Color on_col) {
    Color col = on ? on_col : COL_TEXT_DIM;
    set_color(r, col);
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius)
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    }
    if (on) {
        Color glow = {(Uint8)(on_col.r / 3), (Uint8)(on_col.g / 3), (Uint8)(on_col.b / 3), 100};
        set_color(r, glow);
        for (int dy = -radius - 3; dy <= radius + 3; dy++) {
            for (int dx = -radius - 3; dx <= radius + 3; dx++) {
                int d2 = dx * dx + dy * dy;
                if (d2 <= (radius + 3) * (radius + 3) && d2 > radius * radius)
                    SDL_RenderDrawPoint(r, cx + dx, cy + dy);
            }
        }
    }
}

/**
 * Draw a single VFD dot with glow effect.
 */
static void draw_vfd_dot(SDL_Renderer *r, int cx, int cy, int radius, Color col, bool on) {
    if (!on) {
        Color dim = {(Uint8)(col.r / 20), (Uint8)(col.g / 20), (Uint8)(col.b / 20), 60};
        set_color(r, dim);
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx * dx + dy * dy < radius * radius)
                    SDL_RenderDrawPoint(r, cx + dx, cy + dy);
            }
        }
        return;
    }
    
    /* Glow */
    int glow_r = radius + 1;
    Color glow = {(Uint8)(col.r / 4), (Uint8)(col.g / 4), (Uint8)(col.b / 4), 80};
    set_color(r, glow);
    for (int dy = -glow_r; dy <= glow_r; dy++) {
        for (int dx = -glow_r; dx <= glow_r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= glow_r * glow_r && d2 > radius * radius)
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    }
    
    /* Main */
    set_color(r, col);
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius)
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    }
}

/**
 * Draw a single dot-matrix character.
 */
static void draw_vfd_char(SDL_Renderer *r, int x, int y, int ch_idx,
                          int dot_size, int dot_gap, Color on_col, bool show_off) {
    if (ch_idx < 0 || ch_idx > 13) return;
    
    const uint8_t *pattern = DOT_MATRIX_FONT[ch_idx];
    int dot_spacing = dot_size * 2 + dot_gap;
    Color off_col = {0, 30, 18, 255};
    
    for (int col = 0; col < 5; col++) {
        uint8_t coldata = pattern[col];
        for (int row = 0; row < 7; row++) {
            bool on = (coldata >> row) & 1;
            int px = x + col * dot_spacing + dot_size;
            int py = y + row * dot_spacing + dot_size;
            if (on || show_off) {
                draw_vfd_dot(r, px, py, dot_size, on ? on_col : off_col, on);
            }
        }
    }
}

/**
 * Draw a VFD number string using dot-matrix display.
 */
static int draw_vfd_number(SDL_Renderer *r, int x, int y, const char *str,
                           int dot_size, int dot_gap, int char_gap, Color on_col, bool show_off) {
    int cx = x;
    int char_w = 5 * (dot_size * 2 + dot_gap);
    
    for (const char *p = str; *p; p++) {
        int ch_idx = -1;
        if (*p >= '0' && *p <= '9') {
            ch_idx = *p - '0';
        } else if (*p == '.') {
            ch_idx = 10;
        } else if (*p == '-') {
            ch_idx = 11;
        } else if (*p == '+') {
            ch_idx = 12;
        } else if (*p == ' ') {
            ch_idx = 13;
        } else if (*p == ',') {
            /* Comma - smaller gap, skip */
            cx += char_gap;
            continue;
        }
        
        if (ch_idx >= 0) {
            draw_vfd_char(r, cx, y, ch_idx, dot_size, dot_gap, on_col, show_off);
            if (ch_idx == 10) {
                cx += (dot_size * 2 + dot_gap) * 2 + char_gap;
            } else {
                cx += char_w + char_gap;
            }
        }
    }
    return cx - x;
}

/**
 * Draw a single channel card.
 */
static void draw_channel_card(SDL_Renderer *r, int x, int y, int w, int h,
                              int ch_idx, adc_channel_t *ch, bool selected) {
    Color ch_col = CH_COLORS[ch_idx];
    bool enabled = ch->cal.enabled;
    
    /* Background */
    Color bg = selected ? (Color){45, 45, 52, 255} : COL_BG_PANEL;
    set_color(r, bg);
    fill_rounded_rect(r, x, y, w, h, 3);
    
    /* Left color bar */
    Color bar_col = enabled ? ch_col : COL_TEXT_DIM;
    set_color(r, bar_col);
    fill_rect(r, x, y + 3, 4, h - 6);
    
    /* Border */
    Color border = selected ? COL_ACCENT : COL_BORDER;
    set_color(r, border);
    draw_rect(r, x, y, w, h);
    
    /* Enable checkbox */
    int cb_x = x + 12;
    int cb_y = y + 6;
    set_color(r, COL_BG_WIDGET);
    fill_rect(r, cb_x, cb_y, 14, 14);
    set_color(r, COL_BORDER_LIGHT);
    draw_rect(r, cb_x, cb_y, 14, 14);
    if (enabled) {
        set_color(r, ch_col);
        fill_rect(r, cb_x + 3, cb_y + 3, 8, 8);
    }
    add_button(cb_x, cb_y, 14, 14, BTN_CH_BASE + ch_idx);
    
    /* Channel label */
    draw_text(r, g_app.font_medium, ch->cal.label, x + 32, y + 5, COL_TEXT, 0);
    
    /* Valid indicator */
    draw_led(r, x + w - 12, y + 12, 4, ch->valid, ch_col);
    
    if (!ch->valid || !enabled) {
        draw_text(r, g_app.font_small, "NO DATA", x + 12, y + 30, COL_TEXT_DIM, 0);
        return;
    }
    
    /* Raw count - right aligned, smaller */
    char raw_str[20];
    adc_format_raw(raw_str, sizeof(raw_str), ch->raw);
    draw_text(r, g_app.font_mono, raw_str, x + w - 8, y + 4, COL_TEXT_DIM, 2);
    
    /* Scaled value - VFD style */
    adc_scale_t scale = adc_get_scale(&g_app.adc);
    char val_str[32];
    adc_format_value(val_str, sizeof(val_str), ch->scaled_uv, scale);
    
    /* Split value and unit */
    char *unit_ptr = strrchr(val_str, ' ');
    if (unit_ptr) {
        *unit_ptr = '\0';
        unit_ptr++;
    }
    
    /* Draw VFD number */
    Color vfd_col = enabled ? ch_col : COL_TEXT_DIM;
    int vfd_y = y + 26;
    draw_vfd_number(r, x + 12, vfd_y, val_str, 2, 1, 2, vfd_col, true);
    
    /* Unit */
    if (unit_ptr) {
        draw_text(r, g_app.font_medium, unit_ptr, x + w - 8, vfd_y + 8, COL_LABEL, 2);
    }
    
    /* Statistics row */
    int stat_y = y + h - 20;
    adc_stats_t *st = &ch->stats;
    
    if (st->sample_count > 0) {
        char stat_str[64];
        
        if (scale == ADC_SCALE_UV) {
            snprintf(stat_str, sizeof(stat_str), "Min:%+.0f Max:%+.0f Avg:%+.0f n:%u",
                     st->min_uv, st->max_uv, st->avg_uv, st->sample_count);
        } else if (scale == ADC_SCALE_MV) {
            snprintf(stat_str, sizeof(stat_str), "Min:%+.3f Max:%+.3f Avg:%+.3f mV n:%u",
                     st->min_uv / 1000.0, st->max_uv / 1000.0, st->avg_uv / 1000.0, st->sample_count);
        } else {
            snprintf(stat_str, sizeof(stat_str), "Min:%+.6f Max:%+.6f Avg:%+.6f V n:%u",
                     st->min_uv / 1e6, st->max_uv / 1e6, st->avg_uv / 1e6, st->sample_count);
        }
        draw_text(r, g_app.font_small, stat_str, x + 12, stat_y, COL_TEXT_DIM, 0);
    }
}

/**
 * Draw oscilloscope with multiple channel traces.
 */
static void draw_scope(SDL_Renderer *r, int x, int y, int w, int h) {
    /* Background */
    set_color(r, COL_SCOPE_BG);
    fill_rect(r, x, y, w, h);
    
    /* Grid */
    set_color(r, COL_SCOPE_GRID);
    for (int i = 1; i < 8; i++) {
        int gy = y + h * i / 8;
        SDL_RenderDrawLine(r, x, gy, x + w, gy);
    }
    for (int i = 1; i < 10; i++) {
        int gx = x + w * i / 10;
        SDL_RenderDrawLine(r, gx, y, gx, y + h);
    }
    
    /* Center line */
    SDL_SetRenderDrawColor(r, 40, 60, 45, 255);
    SDL_RenderDrawLine(r, x, y + h / 2, x + w, y + h / 2);
    
    /* Find global min/max for auto-scale */
    double global_min = 1e18, global_max = -1e18;
    int enabled_count = 0;
    
    for (int ch = 0; ch < ADC_MAX_CHANNELS; ch++) {
        adc_channel_t *c = &g_app.ch_cache[ch];
        if (!c->cal.enabled || c->trace.count < 2) continue;
        enabled_count++;
        
        for (int i = 0; i < c->trace.count; i++) {
            int idx = (c->trace.head - c->trace.count + i + ADC_TRACE_LEN) % ADC_TRACE_LEN;
            double v = c->trace.values[idx];
            if (v < global_min) global_min = v;
            if (v > global_max) global_max = v;
        }
    }
    
    if (enabled_count == 0) {
        set_color(r, COL_BORDER);
        draw_rect(r, x, y, w, h);
        draw_text(r, g_app.font_medium, "NO ENABLED CHANNELS", x + w / 2, y + h / 2, COL_TEXT_DIM, 1);
        return;
    }
    
    /* Add margin to range */
    double range = global_max - global_min;
    if (range < 1.0) range = 1.0;
    global_min -= range * 0.1;
    global_max += range * 0.1;
    range = global_max - global_min;
    
    /* Plot area */
    int plot_x = x + 4;
    int plot_w = w - 8;
    int plot_y = y + 4;
    int plot_h = h - 8;
    
    /* Draw traces */
    for (int ch = 0; ch < ADC_MAX_CHANNELS; ch++) {
        adc_channel_t *c = &g_app.ch_cache[ch];
        if (!c->cal.enabled || c->trace.count < 2) continue;
        
        Color col = CH_COLORS[ch];
        set_color(r, col);
        
        int prev_px = -1, prev_py = -1;
        int samples = c->trace.count;
        
        for (int i = 0; i < samples; i++) {
            int idx = (c->trace.head - samples + i + ADC_TRACE_LEN) % ADC_TRACE_LEN;
            double v = c->trace.values[idx];
            
            int px = plot_x + i * plot_w / ADC_TRACE_LEN;
            int py = plot_y + plot_h - (int)((v - global_min) / range * plot_h);
            
            if (py < plot_y) py = plot_y;
            if (py > plot_y + plot_h - 1) py = plot_y + plot_h - 1;
            
            if (prev_px >= 0) {
                SDL_RenderDrawLine(r, prev_px, prev_py, px, py);
            }
            prev_px = px;
            prev_py = py;
        }
    }
    
    /* Border */
    set_color(r, COL_BORDER);
    draw_rect(r, x, y, w, h);
    
    /* Scale labels */
    adc_scale_t scale = adc_get_scale(&g_app.adc);
    char scale_str[24];
    double div = 1.0;
    const char *unit = "µV";
    
    if (scale == ADC_SCALE_MV) { div = 1000.0; unit = "mV"; }
    else if (scale == ADC_SCALE_V) { div = 1e6; unit = "V"; }
    
    snprintf(scale_str, sizeof(scale_str), "%+.3f %s", global_max / div, unit);
    draw_text(r, g_app.font_small, scale_str, x + 5, y + 2, COL_TEXT_DIM, 0);
    
    snprintf(scale_str, sizeof(scale_str), "%+.3f %s", global_min / div, unit);
    draw_text(r, g_app.font_small, scale_str, x + 5, y + h - 14, COL_TEXT_DIM, 0);
    
    /* Legend */
    int leg_x = x + w - 10;
    int leg_y = y + 5;
    for (int ch = ADC_MAX_CHANNELS - 1; ch >= 0; ch--) {
        if (!g_app.ch_cache[ch].cal.enabled) continue;
        
        Color col = CH_COLORS[ch];
        set_color(r, col);
        SDL_RenderDrawLine(r, leg_x - 30, leg_y + 5, leg_x - 15, leg_y + 5);
        
        char lbl[8];
        snprintf(lbl, sizeof(lbl), "CH%d", ch + 1);
        draw_text(r, g_app.font_small, lbl, leg_x, leg_y, col, 2);
        leg_y += 14;
    }
}

/**
 * Draw calibration panel for selected channel.
 */
static void draw_cal_panel(SDL_Renderer *r, int x, int y, int w, int h, int ch_idx) {
    set_color(r, COL_BG_PANEL);
    fill_rounded_rect(r, x, y, w, h, 4);
    set_color(r, COL_BORDER);
    draw_rect(r, x, y, w, h);
    
    /* Title */
    char title[32];
    snprintf(title, sizeof(title), "CALIBRATION - CH%d", ch_idx + 1);
    draw_text(r, g_app.font_medium, title, x + 10, y + 8, CH_COLORS[ch_idx], 0);
    
    adc_channel_t *ch = &g_app.ch_cache[ch_idx];
    int row_y = y + 35;
    int row_h = 28;
    
    /* Offset */
    draw_text(r, g_app.font_small, "Offset:", x + 10, row_y + 6, COL_LABEL, 0);
    char val_str[32];
    snprintf(val_str, sizeof(val_str), "%.2f", ch->cal.offset);
    
    bool editing = (g_app.cal_edit_ch == ch_idx && g_app.cal_edit_field == 0);
    Color field_col = editing ? COL_ACCENT : COL_TEXT;
    draw_text(r, g_app.font_mono, editing ? g_app.cal_input : val_str, x + 80, row_y + 6, field_col, 0);
    add_button(x + 80, row_y, w - 90, row_h, BTN_CAL_BASE + ch_idx * 10 + 0);
    
    row_y += row_h;
    
    /* Gain */
    draw_text(r, g_app.font_small, "Gain:", x + 10, row_y + 6, COL_LABEL, 0);
    snprintf(val_str, sizeof(val_str), "%.6f", ch->cal.gain);
    editing = (g_app.cal_edit_ch == ch_idx && g_app.cal_edit_field == 1);
    field_col = editing ? COL_ACCENT : COL_TEXT;
    draw_text(r, g_app.font_mono, editing ? g_app.cal_input : val_str, x + 80, row_y + 6, field_col, 0);
    add_button(x + 80, row_y, w - 90, row_h, BTN_CAL_BASE + ch_idx * 10 + 1);
    
    row_y += row_h;
    
    /* Vref */
    draw_text(r, g_app.font_small, "Vref:", x + 10, row_y + 6, COL_LABEL, 0);
    snprintf(val_str, sizeof(val_str), "%.0f µV", ch->cal.vref);
    editing = (g_app.cal_edit_ch == ch_idx && g_app.cal_edit_field == 2);
    field_col = editing ? COL_ACCENT : COL_TEXT;
    draw_text(r, g_app.font_mono, editing ? g_app.cal_input : val_str, x + 80, row_y + 6, field_col, 0);
    add_button(x + 80, row_y, w - 90, row_h, BTN_CAL_BASE + ch_idx * 10 + 2);
}

static void draw_header(SDL_Renderer *r) {
    set_color(r, COL_HEADER);
    fill_rect(r, 0, 0, WIN_W, HEADER_H);
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, HEADER_H - 1, WIN_W, HEADER_H - 1);
    
    draw_text(r, g_app.font_title, "24-BIT ADC MONITOR", 20, 10, COL_TEXT, 0);
    draw_text(r, g_app.font_small, "8 CHANNELS", 250, 14, COL_TEXT_DIM, 0);
    
    /* Connection status */
    bool connected = adc_is_connected(&g_app.adc);
    Color status_col = connected ? COL_SUCCESS : (g_app.demo_mode ? COL_WARNING : COL_ERROR);
    const char *status_txt = connected ? "ONLINE" : (g_app.demo_mode ? "DEMO" : "OFFLINE");
    draw_led(r, WIN_W - 100, 22, 5, true, status_col);
    draw_text(r, g_app.font_medium, status_txt, WIN_W - 88, 14, status_col, 0);
    
    /* Stats */
    char stats[64];
    uint32_t rx, err, perr;
    adc_get_stats(&g_app.adc, &rx, &err, &perr);
    snprintf(stats, sizeof(stats), "FPS:%d RX:%u ERR:%u", g_app.fps, rx, perr);
    draw_text(r, g_app.font_small, stats, WIN_W - 220, 30, COL_TEXT_DIM, 0);
}

static void draw_toolbar(SDL_Renderer *r) {
    int y = HEADER_H;
    set_color(r, COL_BG_WIDGET);
    fill_rect(r, 0, y, WIN_W, TOOLBAR_H);
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, y + TOOLBAR_H - 1, WIN_W, y + TOOLBAR_H - 1);
    
    /* Scale buttons */
    draw_text(r, g_app.font_small, "SCALE:", 20, y + 12, COL_LABEL, 0);
    
    adc_scale_t scale = adc_get_scale(&g_app.adc);
    draw_button(r, 80, y + 6, 45, 24, "µV", scale == ADC_SCALE_UV, g_app.hover_btn == BTN_SCALE_UV, BTN_SCALE_UV);
    draw_button(r, 130, y + 6, 45, 24, "mV", scale == ADC_SCALE_MV, g_app.hover_btn == BTN_SCALE_MV, BTN_SCALE_MV);
    draw_button(r, 180, y + 6, 45, 24, "V", scale == ADC_SCALE_V, g_app.hover_btn == BTN_SCALE_V, BTN_SCALE_V);
    
    /* Stats controls */
    draw_text(r, g_app.font_small, "STATISTICS:", 260, y + 12, COL_LABEL, 0);
    draw_button(r, 350, y + 6, 60, 24, "RESET", false, g_app.hover_btn == BTN_STATS_RESET, BTN_STATS_RESET);
    
    bool stats_running = true;  /* TODO: get from context */
    draw_button(r, 420, y + 6, 60, 24, stats_running ? "PAUSE" : "RUN", 
                !stats_running, g_app.hover_btn == BTN_STATS_PAUSE, BTN_STATS_PAUSE);
}

static void render(void) {
    SDL_Renderer *r = g_app.renderer;
    set_color(r, COL_BG_DARK);
    SDL_RenderClear(r);
    g_app.num_buttons = 0;
    
    draw_header(r);
    draw_toolbar(r);
    
    int content_y = HEADER_H + TOOLBAR_H;
    int content_h = WIN_H - content_y;
    
    /* Left sidebar - channel cards */
    int sidebar_x = 10;
    int sidebar_y = content_y + 10;
    int card_y = sidebar_y;
    
    for (int ch = 0; ch < ADC_MAX_CHANNELS; ch++) {
        bool selected = (g_app.selected_ch == ch);
        draw_channel_card(r, sidebar_x, card_y, SIDEBAR_W, CH_CARD_H, ch, 
                          &g_app.ch_cache[ch], selected);
        add_button(sidebar_x, card_y, SIDEBAR_W, CH_CARD_H, BTN_CH_BASE + 50 + ch);  /* Selection button */
        card_y += CH_CARD_H + CH_CARD_GAP;
    }
    
    /* Right area - scope */
    int scope_x = sidebar_x + SIDEBAR_W + SCOPE_MARGIN;
    int scope_y = content_y + SCOPE_MARGIN;
    int scope_w = WIN_W - scope_x - SCOPE_MARGIN;
    int scope_h = content_h - SCOPE_MARGIN * 2;
    
    /* If a channel is selected, show calibration panel at bottom */
    if (g_app.selected_ch >= 0) {
        int cal_h = 130;
        scope_h -= cal_h + SCOPE_MARGIN;
        draw_scope(r, scope_x, scope_y, scope_w, scope_h);
        draw_cal_panel(r, scope_x, scope_y + scope_h + SCOPE_MARGIN, scope_w, cal_h, g_app.selected_ch);
    } else {
        draw_scope(r, scope_x, scope_y, scope_w, scope_h);
    }
    
    SDL_RenderPresent(r);
}

static void update_from_adc(void) {
    for (int i = 0; i < ADC_MAX_CHANNELS; i++) {
        adc_get_channel(&g_app.adc, i, &g_app.ch_cache[i]);
    }
}

static void update_demo(void) {
    static float phase = 0.0f;
    phase += 0.05f;
    
    uint64_t now = get_time_ms();
    
    for (int i = 0; i < ADC_MAX_CHANNELS; i++) {
        adc_channel_t *ch = &g_app.ch_cache[i];
        
        /* Generate demo data with different frequencies */
        float freq = 1.0f + i * 0.3f;
        float amp = 1000000.0f + i * 500000.0f;  /* µV */
        float offset_uv = (i - 4) * 2000000.0f;
        
        double val_uv = offset_uv + amp * sinf(phase * freq + i * 0.5f);
        val_uv += (rand() % 10000) - 5000;  /* Noise */
        
        /* Convert back to raw (assuming default cal) */
        ch->raw = (int32_t)(val_uv / (ch->cal.vref / 8388608.0) / ch->cal.gain + ch->cal.offset);
        ch->scaled_uv = val_uv;
        ch->valid = true;
        ch->timestamp_ms = now;
        
        /* Update trace */
        ch->trace.raw[ch->trace.head] = ch->raw;
        ch->trace.values[ch->trace.head] = ch->scaled_uv;
        ch->trace.head = (ch->trace.head + 1) % ADC_TRACE_LEN;
        if (ch->trace.count < ADC_TRACE_LEN) ch->trace.count++;
        
        /* Update stats */
        if (ch->stats.sample_count == 0) {
            ch->stats.min_raw = ch->raw;
            ch->stats.max_raw = ch->raw;
            ch->stats.min_uv = val_uv;
            ch->stats.max_uv = val_uv;
        } else {
            if (val_uv < ch->stats.min_uv) { ch->stats.min_uv = val_uv; ch->stats.min_raw = ch->raw; }
            if (val_uv > ch->stats.max_uv) { ch->stats.max_uv = val_uv; ch->stats.max_raw = ch->raw; }
        }
        ch->stats.sum_raw += ch->raw;
        ch->stats.sample_count++;
        ch->stats.avg_uv = (ch->stats.min_uv + ch->stats.max_uv) / 2;  /* Simplified */
    }
}

static void handle_click(int mx, int my) {
    int btn = button_at(mx, my);
    if (btn == 0) {
        g_app.cal_edit_ch = -1;
        return;
    }
    
    switch (btn) {
        case BTN_SCALE_UV:
            adc_set_scale(&g_app.adc, ADC_SCALE_UV);
            break;
        case BTN_SCALE_MV:
            adc_set_scale(&g_app.adc, ADC_SCALE_MV);
            break;
        case BTN_SCALE_V:
            adc_set_scale(&g_app.adc, ADC_SCALE_V);
            break;
        case BTN_STATS_RESET:
            adc_reset_stats(&g_app.adc, -1);
            for (int i = 0; i < ADC_MAX_CHANNELS; i++) {
                memset(&g_app.ch_cache[i].stats, 0, sizeof(adc_stats_t));
            }
            break;
        case BTN_STATS_PAUSE:
            /* TODO: implement pause */
            break;
        default:
            /* Channel enable toggles */
            if (btn >= BTN_CH_BASE && btn < BTN_CH_BASE + ADC_MAX_CHANNELS) {
                int ch = btn - BTN_CH_BASE;
                bool new_state = !g_app.ch_cache[ch].cal.enabled;
                adc_set_channel_enabled(&g_app.adc, ch, new_state);
                g_app.ch_cache[ch].cal.enabled = new_state;
            }
            /* Channel selection (card click) */
            else if (btn >= BTN_CH_BASE + 50 && btn < BTN_CH_BASE + 50 + ADC_MAX_CHANNELS) {
                int ch = btn - BTN_CH_BASE - 50;
                g_app.selected_ch = (g_app.selected_ch == ch) ? -1 : ch;
                g_app.cal_edit_ch = -1;
            }
            /* Calibration field edits */
            else if (btn >= BTN_CAL_BASE && btn < BTN_CAL_BASE + 80) {
                int encoded = btn - BTN_CAL_BASE;
                int ch = encoded / 10;
                int field = encoded % 10;
                g_app.cal_edit_ch = ch;
                g_app.cal_edit_field = field;
                
                /* Initialize input with current value */
                adc_channel_t *c = &g_app.ch_cache[ch];
                if (field == 0) snprintf(g_app.cal_input, sizeof(g_app.cal_input), "%.2f", c->cal.offset);
                else if (field == 1) snprintf(g_app.cal_input, sizeof(g_app.cal_input), "%.6f", c->cal.gain);
                else snprintf(g_app.cal_input, sizeof(g_app.cal_input), "%.0f", c->cal.vref);
            }
            break;
    }
}

static void apply_cal_edit(void) {
    if (g_app.cal_edit_ch < 0) return;
    
    int ch = g_app.cal_edit_ch;
    int field = g_app.cal_edit_field;
    double val = atof(g_app.cal_input);
    
    adc_channel_t *c = &g_app.ch_cache[ch];
    double offset = c->cal.offset;
    double gain = c->cal.gain;
    double vref = c->cal.vref;
    
    if (field == 0) offset = val;
    else if (field == 1) gain = val;
    else vref = val;
    
    adc_set_calibration(&g_app.adc, ch, offset, gain, vref);
    g_app.ch_cache[ch].cal.offset = offset;
    g_app.ch_cache[ch].cal.gain = gain;
    g_app.ch_cache[ch].cal.vref = vref;
    
    g_app.cal_edit_ch = -1;
}

static void handle_key(SDL_Keycode key) {
    if (g_app.cal_edit_ch >= 0) {
        size_t len = strlen(g_app.cal_input);
        
        if (key == SDLK_BACKSPACE && len > 0) {
            g_app.cal_input[len - 1] = '\0';
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            apply_cal_edit();
        } else if (key == SDLK_ESCAPE) {
            g_app.cal_edit_ch = -1;
        }
        return;
    }
    
    /* Global shortcuts */
    if (key == SDLK_1) adc_set_scale(&g_app.adc, ADC_SCALE_UV);
    else if (key == SDLK_2) adc_set_scale(&g_app.adc, ADC_SCALE_MV);
    else if (key == SDLK_3) adc_set_scale(&g_app.adc, ADC_SCALE_V);
    else if (key == SDLK_r) adc_reset_stats(&g_app.adc, -1);
    else if (key == SDLK_ESCAPE) g_app.selected_ch = -1;
}

static void handle_text(const char *text) {
    if (g_app.cal_edit_ch < 0) return;
    
    for (const char *p = text; *p; p++) {
        if ((*p >= '0' && *p <= '9') || *p == '.' || *p == '-' || *p == '+') {
            size_t len = strlen(g_app.cal_input);
            if (len < sizeof(g_app.cal_input) - 1) {
                g_app.cal_input[len] = *p;
                g_app.cal_input[len + 1] = '\0';
            }
        }
    }
}

static bool init_app(const char *serial_device) {
    memset(&g_app, 0, sizeof(g_app));
    g_app.running = true;
    g_app.selected_ch = -1;
    g_app.cal_edit_ch = -1;
    
    /* Initialize channel cache with defaults */
    for (int i = 0; i < ADC_MAX_CHANNELS; i++) {
        g_app.ch_cache[i].cal.offset = 0.0;
        g_app.ch_cache[i].cal.gain = 1.0;
        g_app.ch_cache[i].cal.vref = 10000000.0;
        g_app.ch_cache[i].cal.enabled = true;
        snprintf(g_app.ch_cache[i].cal.label, sizeof(g_app.ch_cache[i].cal.label), "CH%d", i + 1);
    }
    
    if (!adc_init(&g_app.adc, serial_device, 115200)) {
        printf("Could not open %s, running in DEMO mode\n", serial_device);
        g_app.demo_mode = true;
        
        /* Still need to set up scale in demo mode */
        g_app.adc.display_scale = ADC_SCALE_MV;
    } else {
        printf("Connected to %s\n", serial_device);
    }
    
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return false;
    }
    
    g_app.window = SDL_CreateWindow("24-bit ADC Monitor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!g_app.window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        TTF_Quit(); SDL_Quit();
        return false;
    }
    
    g_app.renderer = SDL_CreateRenderer(g_app.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_app.renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_app.window);
        TTF_Quit(); SDL_Quit();
        return false;
    }
    SDL_SetRenderDrawBlendMode(g_app.renderer, SDL_BLENDMODE_BLEND);
    
    const char *font_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
        NULL
    };
    const char *font_path = NULL;
    for (int i = 0; font_paths[i]; i++) {
        FILE *f = fopen(font_paths[i], "r");
        if (f) { fclose(f); font_path = font_paths[i]; break; }
    }
    if (!font_path) {
        fprintf(stderr, "No suitable font found.\n");
        SDL_DestroyRenderer(g_app.renderer);
        SDL_DestroyWindow(g_app.window);
        TTF_Quit(); SDL_Quit();
        return false;
    }
    
    g_app.font_title = TTF_OpenFont(font_path, 20);
    g_app.font_large = TTF_OpenFont(font_path, 16);
    g_app.font_medium = TTF_OpenFont(font_path, 12);
    g_app.font_small = TTF_OpenFont(font_path, 10);
    g_app.font_mono = TTF_OpenFont(font_path, 11);
    g_app.font_mono_large = TTF_OpenFont(font_path, 14);
    
    if (!g_app.font_title || !g_app.font_large || !g_app.font_medium ||
        !g_app.font_small || !g_app.font_mono || !g_app.font_mono_large) {
        fprintf(stderr, "Failed to load fonts\n");
        SDL_DestroyRenderer(g_app.renderer);
        SDL_DestroyWindow(g_app.window);
        TTF_Quit(); SDL_Quit();
        return false;
    }
    
    g_app.last_fps_time = SDL_GetTicks();
    return true;
}

static void cleanup_app(void) {
    if (!g_app.demo_mode) {
        adc_shutdown(&g_app.adc);
    }
    if (g_app.font_title) TTF_CloseFont(g_app.font_title);
    if (g_app.font_large) TTF_CloseFont(g_app.font_large);
    if (g_app.font_medium) TTF_CloseFont(g_app.font_medium);
    if (g_app.font_small) TTF_CloseFont(g_app.font_small);
    if (g_app.font_mono) TTF_CloseFont(g_app.font_mono);
    if (g_app.font_mono_large) TTF_CloseFont(g_app.font_mono_large);
    if (g_app.renderer) SDL_DestroyRenderer(g_app.renderer);
    if (g_app.window) SDL_DestroyWindow(g_app.window);
    TTF_Quit();
    SDL_Quit();
}

int main(int argc, char *argv[]) {
    const char *serial_device = "/dev/ttyUSB0";
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("24-bit ADC Monitor\n");
            printf("Usage: %s [serial_port]\n", argv[0]);
            printf("  Default: /dev/ttyUSB0\n");
            printf("  Runs in DEMO mode if port unavailable.\n");
            printf("\nSerial format: CH1+12345678CH2-00012345...CH8+xxxxxxxx\\n\n");
            printf("Keyboard shortcuts:\n");
            printf("  1/2/3  - Scale µV/mV/V\n");
            printf("  r      - Reset statistics\n");
            printf("  ESC    - Deselect channel\n");
            return 0;
        }
        serial_device = argv[1];
    }
    
    if (!init_app(serial_device)) return 1;
    
    while (g_app.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT: 
                    g_app.running = false; 
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (ev.button.button == SDL_BUTTON_LEFT) 
                        handle_click(ev.button.x, ev.button.y);
                    break;
                case SDL_MOUSEMOTION:
                    g_app.hover_btn = button_at(ev.motion.x, ev.motion.y);
                    break;
                case SDL_KEYDOWN: 
                    handle_key(ev.key.keysym.sym); 
                    break;
                case SDL_TEXTINPUT: 
                    handle_text(ev.text.text); 
                    break;
            }
        }
        
        if (g_app.demo_mode) {
            update_demo();
        } else {
            update_from_adc();
        }
        
        render();
        
        /* FPS counter */
        g_app.frame_count++;
        uint32_t now = SDL_GetTicks();
        if (now - g_app.last_fps_time >= 1000) {
            g_app.fps = g_app.frame_count;
            g_app.frame_count = 0;
            g_app.last_fps_time = now;
        }
        
        SDL_Delay(8);  /* ~120 FPS max */
    }
    
    cleanup_app();
    return 0;
}
