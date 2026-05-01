/*
 * Bench Multimeter (DMM) GUI.
 *
 * Standalone SDL2 application styled after the imported PSU/ADC GUIs:
 * dark panel theme, VFD-style green readout, cyan accent, common widget
 * sizes. Demo mode simulates a slowly-drifting signal with noise so the
 * UI is exercisable without hardware.
 *
 * The first argv[1] is reserved for a serial device path (matching the
 * other instrument tools) but the protocol is not implemented — the GUI
 * is purely a UI scaffold ready for a future serial backend.
 */
#define _POSIX_C_SOURCE 200809L

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vfd.h"

#define WIN_W 760
#define WIN_H 460
#define HEADER_H 36
#define FOOTER_H 28
#define MODE_BAR_W 84
#define STATS_W 200
#define PAD 10

typedef struct {
    Uint8 r, g, b, a;
} Color;

static const Color COL_BG_DARK = { 30, 30, 32, 255 };
static const Color COL_BG_PANEL = { 42, 42, 46, 255 };
static const Color COL_BG_WIDGET __attribute__((unused)) = { 28, 28, 30, 255 };
static const Color COL_HEADER = { 24, 24, 26, 255 };
static const Color COL_BORDER = { 60, 60, 65, 255 };
static const Color COL_BORDER_LIGHT = { 80, 80, 88, 255 };
static const Color COL_TEXT = { 200, 200, 205, 255 };
static const Color COL_TEXT_DIM = { 120, 120, 128, 255 };
static const Color COL_LABEL = { 160, 160, 168, 255 };
static const Color COL_ACCENT = { 0, 180, 220, 255 };
static const Color COL_SUCCESS = { 50, 205, 100, 255 };
static const Color COL_WARNING = { 255, 180, 0, 255 };
static const Color COL_VFD_BG = { 8, 18, 12, 255 };
static const Color COL_VFD_ON = { 0, 255, 120, 255 };
static const Color COL_VFD_OFF = { 0, 60, 35, 255 };
static const Color COL_BTN_NORMAL = { 50, 50, 55, 255 };
static const Color COL_BTN_HOVER = { 65, 65, 72, 255 };
static const Color COL_BTN_ACTIVE = { 0, 140, 170, 255 };

static const char *const FONT_CANDIDATES[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    NULL,
};

typedef enum {
    MODE_VDC = 0,
    MODE_VAC,
    MODE_ADC,
    MODE_AAC,
    MODE_OHM,
    MODE_HZ,
    MODE_COUNT,
} DmmMode;

static const char *const MODE_NAMES[MODE_COUNT] = { "V DC", "V AC", "A DC",
                                                    "A AC", "Ω",    "Hz" };
/* Demo target ranges (center, jitter, noise) per mode — the simulator
 * lazily wanders within these bands so the readout looks alive. */
static const double MODE_DEMO_CENTER[MODE_COUNT] = { 12.0, 230.0,  1.5,
                                                     0.05, 1000.0, 60.0 };
static const double MODE_DEMO_JITTER[MODE_COUNT] = { 0.05, 0.5,    0.05,
                                                     0.005, 5.0,   0.01 };

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font_huge;
    TTF_Font *font_large;
    TTF_Font *font_med;
    TTF_Font *font_small;

    DmmMode mode;
    bool hold;
    bool autorange;

    /* Latest measurement (demo simulator output) */
    double value;
    double sim_phase;

    /* Statistics over the active session (cleared on RESET / mode change). */
    double stat_min, stat_max, stat_sum;
    long stat_n;
    bool stat_valid;

    /* Hover/click hit boxes — recomputed every frame in compute_layout. */
    SDL_Rect mode_hits[MODE_COUNT];
    SDL_Rect hold_hit, autorange_hit, reset_hit;
    int hover_mode; /* -1 = none */
    int hover_btn;  /* 0=none, 1=hold, 2=autorange, 3=reset */

    bool demo_mode;
    char serial_path[256];
} App;

static App g_app;

/* ------------------------------------------------------------------------- */

static void set_color(SDL_Renderer *r, Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}

static void stroke_rect(SDL_Renderer *r, int x, int y, int w, int h)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderDrawRect(r, &rect);
}

static void draw_text(SDL_Renderer *r, TTF_Font *font, const char *text, int x,
                      int y, Color fg, int align_right_x)
{
    SDL_Color c = { fg.r, fg.g, fg.b, fg.a };
    SDL_Surface *surf;
    SDL_Texture *tex;
    SDL_Rect dst;

    if (!text || !text[0])
        return;
    surf = TTF_RenderUTF8_Blended(font, text, c);
    if (!surf)
        return;
    tex = SDL_CreateTextureFromSurface(r, surf);
    if (!tex) {
        SDL_FreeSurface(surf);
        return;
    }
    dst.w = surf->w;
    dst.h = surf->h;
    dst.x = align_right_x ? (align_right_x - surf->w) : x;
    dst.y = y;
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static void draw_text_centered(SDL_Renderer *r, TTF_Font *font,
                               const char *text, int cx, int cy, Color fg)
{
    int tw = 0, th = 0;

    if (!text || !text[0])
        return;
    if (TTF_SizeUTF8(font, text, &tw, &th) != 0)
        return;
    draw_text(r, font, text, cx - tw / 2, cy - th / 2, fg, 0);
}

static bool point_in_rect(int x, int y, SDL_Rect rect)
{
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y &&
           y < rect.y + rect.h;
}

static void draw_led(SDL_Renderer *r, int cx, int cy, int radius, bool on,
                     Color color)
{
    int dy;
    Color c = on ? color : (Color){ color.r / 4, color.g / 4, color.b / 4, 255 };

    set_color(r, c);
    for (dy = -radius; dy <= radius; dy++) {
        int dx = (int)sqrt((double)(radius * radius - dy * dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

/* ------------------------------------------------------------------------- */

static void format_measurement(DmmMode m, double v, char *out, size_t outsz,
                               char *unit_out, size_t unitsz)
{
    const char *unit = "";
    double mag = fabs(v);
    double scale = 1.0;
    char prefix[4] = "";

    switch (m) {
    case MODE_VDC:
    case MODE_VAC:
        unit = "V";
        if (mag < 1.0) {
            scale = 1000.0;
            snprintf(prefix, sizeof(prefix), "m");
        } else if (mag < 0.001) {
            scale = 1e6;
            snprintf(prefix, sizeof(prefix), "µ");
        }
        break;
    case MODE_ADC:
    case MODE_AAC:
        unit = "A";
        if (mag < 0.001) {
            scale = 1e6;
            snprintf(prefix, sizeof(prefix), "µ");
        } else if (mag < 1.0) {
            scale = 1000.0;
            snprintf(prefix, sizeof(prefix), "m");
        }
        break;
    case MODE_OHM:
        unit = "Ω";
        if (mag >= 1e6) {
            scale = 1e-6;
            snprintf(prefix, sizeof(prefix), "M");
        } else if (mag >= 1e3) {
            scale = 1e-3;
            snprintf(prefix, sizeof(prefix), "k");
        }
        break;
    case MODE_HZ:
        unit = "Hz";
        if (mag >= 1e6) {
            scale = 1e-6;
            snprintf(prefix, sizeof(prefix), "M");
        } else if (mag >= 1e3) {
            scale = 1e-3;
            snprintf(prefix, sizeof(prefix), "k");
        }
        break;
    default:
        unit = "";
        break;
    }
    snprintf(out, outsz, "%+8.3f", v * scale);
    snprintf(unit_out, unitsz, "%s%s", prefix, unit);
}

/* ------------------------------------------------------------------------- */

static void reset_stats(App *a)
{
    a->stat_valid = false;
    a->stat_min = 0;
    a->stat_max = 0;
    a->stat_sum = 0;
    a->stat_n = 0;
}

static void update_stats(App *a, double v)
{
    if (!a->stat_valid) {
        a->stat_min = v;
        a->stat_max = v;
        a->stat_sum = v;
        a->stat_n = 1;
        a->stat_valid = true;
        return;
    }
    if (v < a->stat_min)
        a->stat_min = v;
    if (v > a->stat_max)
        a->stat_max = v;
    a->stat_sum += v;
    a->stat_n++;
}

/* Demo simulator: a slow drift around the mode's center plus per-frame
 * Gaussian-ish noise. Keeps the readout believable in lieu of hardware. */
static double sim_value(App *a)
{
    double center = MODE_DEMO_CENTER[a->mode];
    double jitter = MODE_DEMO_JITTER[a->mode];
    double drift = jitter * 0.3 * sin(a->sim_phase);
    double noise = jitter * (((double)rand() / RAND_MAX) - 0.5);

    a->sim_phase += 0.07;
    if (a->sim_phase > 6.28318)
        a->sim_phase -= 6.28318;
    return center + drift + noise;
}

/* ------------------------------------------------------------------------- */

static void compute_layout(App *a, int win_w, int win_h)
{
    int top_y = HEADER_H + PAD;
    int bot_y = win_h - FOOTER_H - PAD;
    int avail_h = bot_y - top_y;
    int btn_h = avail_h / MODE_COUNT;
    int i;

    if (btn_h < 36)
        btn_h = 36;
    if (btn_h > 64)
        btn_h = 64;

    /* Mode bar on the left */
    for (i = 0; i < MODE_COUNT; i++) {
        a->mode_hits[i].x = PAD;
        a->mode_hits[i].y = top_y + i * btn_h;
        a->mode_hits[i].w = MODE_BAR_W;
        a->mode_hits[i].h = btn_h - 4;
    }

    /* Footer buttons */
    a->hold_hit.x = PAD;
    a->hold_hit.y = win_h - FOOTER_H + 2;
    a->hold_hit.w = 80;
    a->hold_hit.h = FOOTER_H - 4;

    a->autorange_hit.x = PAD + 90;
    a->autorange_hit.y = win_h - FOOTER_H + 2;
    a->autorange_hit.w = 80;
    a->autorange_hit.h = FOOTER_H - 4;

    a->reset_hit.x = win_w - PAD - 80;
    a->reset_hit.y = top_y + avail_h - 36;
    a->reset_hit.w = 80;
    a->reset_hit.h = 30;
}

static void draw_header(SDL_Renderer *r, int win_w)
{
    set_color(r, COL_HEADER);
    fill_rect(r, 0, 0, win_w, HEADER_H);
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, HEADER_H - 1, win_w, HEADER_H - 1);
    draw_text(r, g_app.font_med, "BENCH MULTIMETER", PAD, 9, COL_TEXT, 0);

    {
        Color c = g_app.demo_mode ? COL_WARNING : COL_SUCCESS;
        const char *label = g_app.demo_mode ? "DEMO" : "ONLINE";
        int tw = 0;

        TTF_SizeUTF8(g_app.font_small, label, &tw, NULL);
        draw_text(r, g_app.font_small, label, win_w - PAD - tw - 18, 12, c, 0);
        draw_led(r, win_w - PAD - 8, HEADER_H / 2, 5, true, c);
    }
}

static void draw_mode_bar(SDL_Renderer *r, App *a)
{
    int i;

    for (i = 0; i < MODE_COUNT; i++) {
        SDL_Rect b = a->mode_hits[i];
        Color bg = COL_BTN_NORMAL;

        if (a->mode == (DmmMode)i)
            bg = COL_BTN_ACTIVE;
        else if (a->hover_mode == i)
            bg = COL_BTN_HOVER;

        set_color(r, bg);
        SDL_RenderFillRect(r, &b);
        set_color(r, COL_BORDER_LIGHT);
        SDL_RenderDrawRect(r, &b);
        draw_text_centered(r, g_app.font_med, MODE_NAMES[i],
                           b.x + b.w / 2, b.y + b.h / 2, COL_TEXT);
    }
}

static void draw_vfd_panel(SDL_Renderer *r, App *a, int x, int y, int w, int h)
{
    char num[64];
    char unit[16];
    int unit_tw = 0, unit_th = 0;
    VfdColor on_col = { COL_VFD_ON.r, COL_VFD_ON.g, COL_VFD_ON.b,
                        COL_VFD_ON.a };
    VfdColor off_col = { COL_VFD_OFF.r, COL_VFD_OFF.g, COL_VFD_OFF.b, 255 };
    int dot_size, dot_gap, char_gap;
    int num_w;
    int chars;
    int avail_w, avail_h;
    int header_reserve = 24;
    int side_pad = 16;

    set_color(r, COL_VFD_BG);
    fill_rect(r, x, y, w, h);
    set_color(r, COL_BORDER);
    stroke_rect(r, x, y, w, h);
    set_color(r, COL_VFD_OFF);
    stroke_rect(r, x + 2, y + 2, w - 4, h - 4);

    format_measurement(a->mode, a->value, num, sizeof(num), unit, sizeof(unit));

    /* Reserve room on the right for the unit label so we know how much
     * width is available for the dot-matrix number. */
    TTF_SizeUTF8(a->font_large, unit, &unit_tw, &unit_th);

    chars = (int)strlen(num);
    avail_w = w - side_pad * 2 - unit_tw - 24; /* 24 = unit gap */
    avail_h = h - header_reserve - 12;

    /* Auto-size: largest dot that fits both the number's width and the
     * panel's height. Caps avoid absurdly large dots in huge windows. */
    dot_size = vfd_fit_dot_size(chars, avail_w, avail_h, 2, 6);
    dot_gap = 1;
    char_gap = dot_size * 2;

    num_w = vfd_measure(num, dot_size, dot_gap, char_gap);

    {
        int total_w = num_w + 24 + unit_tw;
        int start_x = x + (w - total_w) / 2;
        int dot_h = 7 * (dot_size * 2 + dot_gap);
        int start_y = y + (h - dot_h) / 2;

        if (start_x < x + side_pad)
            start_x = x + side_pad;
        vfd_draw_number(r, start_x, start_y, num, dot_size, dot_gap, char_gap,
                        on_col, off_col, true);
        draw_text(r, a->font_large, unit, start_x + num_w + 24,
                  start_y + dot_h / 2 - unit_th / 2 + 2, COL_VFD_ON, 0);
    }

    /* Range / hold indicator on the top edge */
    {
        char info[64];

        snprintf(info, sizeof(info), "%s%s", a->autorange ? "AUTO" : "MAN",
                 a->hold ? "  HOLD" : "");
        draw_text(r, a->font_small, info, x + 8, y + 6,
                  a->hold ? COL_WARNING : COL_TEXT_DIM, 0);
    }
}

static void draw_stats_panel(SDL_Renderer *r, App *a, int x, int y, int w,
                             int h)
{
    char num[64], unit[16];
    int row_h = 28;
    int rx = x + 12;
    int line_y;

    set_color(r, COL_BG_PANEL);
    fill_rect(r, x, y, w, h);
    set_color(r, COL_BORDER);
    stroke_rect(r, x, y, w, h);

    draw_text(r, a->font_med, "STATS", rx, y + 8, COL_ACCENT, 0);
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, x + 8, y + 32, x + w - 8, y + 32);

    line_y = y + 44;

    if (!a->stat_valid) {
        draw_text(r, a->font_small, "MIN", rx, line_y + 4, COL_LABEL, 0);
        draw_text(r, a->font_small, "----", rx + 50, line_y + 4, COL_TEXT_DIM,
                  0);
        line_y += row_h;
        draw_text(r, a->font_small, "MAX", rx, line_y + 4, COL_LABEL, 0);
        draw_text(r, a->font_small, "----", rx + 50, line_y + 4, COL_TEXT_DIM,
                  0);
        line_y += row_h;
        draw_text(r, a->font_small, "AVG", rx, line_y + 4, COL_LABEL, 0);
        draw_text(r, a->font_small, "----", rx + 50, line_y + 4, COL_TEXT_DIM,
                  0);
    } else {
        format_measurement(a->mode, a->stat_min, num, sizeof(num), unit,
                           sizeof(unit));
        draw_text(r, a->font_small, "MIN", rx, line_y + 4, COL_LABEL, 0);
        draw_text(r, a->font_small, num, rx + 50, line_y + 4, COL_TEXT, 0);
        draw_text(r, a->font_small, unit, rx + w - 50, line_y + 4, COL_TEXT_DIM,
                  0);
        line_y += row_h;
        format_measurement(a->mode, a->stat_max, num, sizeof(num), unit,
                           sizeof(unit));
        draw_text(r, a->font_small, "MAX", rx, line_y + 4, COL_LABEL, 0);
        draw_text(r, a->font_small, num, rx + 50, line_y + 4, COL_TEXT, 0);
        draw_text(r, a->font_small, unit, rx + w - 50, line_y + 4, COL_TEXT_DIM,
                  0);
        line_y += row_h;
        format_measurement(a->mode, a->stat_sum / (double)a->stat_n, num,
                           sizeof(num), unit, sizeof(unit));
        draw_text(r, a->font_small, "AVG", rx, line_y + 4, COL_LABEL, 0);
        draw_text(r, a->font_small, num, rx + 50, line_y + 4, COL_TEXT, 0);
        draw_text(r, a->font_small, unit, rx + w - 50, line_y + 4, COL_TEXT_DIM,
                  0);
    }

    /* Sample count */
    {
        char buf[32];

        snprintf(buf, sizeof(buf), "n = %ld", a->stat_n);
        draw_text(r, a->font_small, buf, rx, y + h - 50, COL_TEXT_DIM, 0);
    }

    /* RESET button */
    {
        SDL_Rect b = a->reset_hit;
        Color bg = (a->hover_btn == 3) ? COL_BTN_HOVER : COL_BTN_NORMAL;

        set_color(r, bg);
        SDL_RenderFillRect(r, &b);
        set_color(r, COL_BORDER_LIGHT);
        SDL_RenderDrawRect(r, &b);
        draw_text_centered(r, a->font_small, "RESET", b.x + b.w / 2,
                           b.y + b.h / 2, COL_TEXT);
    }
}

static void draw_footer_btn(SDL_Renderer *r, App *a, SDL_Rect b,
                            const char *label, bool active, bool hover)
{
    Color bg = active ? COL_BTN_ACTIVE : (hover ? COL_BTN_HOVER : COL_BTN_NORMAL);

    set_color(r, bg);
    SDL_RenderFillRect(r, &b);
    set_color(r, COL_BORDER_LIGHT);
    SDL_RenderDrawRect(r, &b);
    draw_text_centered(r, a->font_small, label, b.x + b.w / 2, b.y + b.h / 2,
                       COL_TEXT);
}

static void draw_footer(SDL_Renderer *r, App *a, int win_w, int win_h)
{
    set_color(r, COL_HEADER);
    fill_rect(r, 0, win_h - FOOTER_H, win_w, FOOTER_H);
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, win_h - FOOTER_H, win_w, win_h - FOOTER_H);

    draw_footer_btn(r, a, a->hold_hit, "HOLD", a->hold, a->hover_btn == 1);
    draw_footer_btn(r, a, a->autorange_hit, a->autorange ? "AUTO" : "MAN",
                    a->autorange, a->hover_btn == 2);

    {
        const char *hint =
            a->demo_mode
                ? "Demo mode — pass a serial device path on the command line for hardware."
                : "Connected.";

        draw_text(r, a->font_small, hint, PAD + 180, win_h - FOOTER_H + 8,
                  COL_TEXT_DIM, 0);
    }
}

/* ------------------------------------------------------------------------- */

static int hit_mode_btn(App *a, int x, int y)
{
    int i;

    for (i = 0; i < MODE_COUNT; i++)
        if (point_in_rect(x, y, a->mode_hits[i]))
            return i;
    return -1;
}

static int hit_other_btn(App *a, int x, int y)
{
    if (point_in_rect(x, y, a->hold_hit))
        return 1;
    if (point_in_rect(x, y, a->autorange_hit))
        return 2;
    if (point_in_rect(x, y, a->reset_hit))
        return 3;
    return 0;
}

static void on_click(App *a, int x, int y)
{
    int m = hit_mode_btn(a, x, y);
    int b;

    if (m >= 0) {
        if ((DmmMode)m != a->mode) {
            a->mode = (DmmMode)m;
            reset_stats(a);
        }
        return;
    }
    b = hit_other_btn(a, x, y);
    if (b == 1)
        a->hold = !a->hold;
    else if (b == 2)
        a->autorange = !a->autorange;
    else if (b == 3)
        reset_stats(a);
}

/* ------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    int running = 1;
    Uint32 last_sample = 0;
    const char *font_path = NULL;
    int i;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    g_app.window =
        SDL_CreateWindow("Bench Multimeter", SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
                         SDL_WINDOW_RESIZABLE);
    if (!g_app.window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    g_app.renderer =
        SDL_CreateRenderer(g_app.window, -1, SDL_RENDERER_ACCELERATED);
    if (!g_app.renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_app.window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    for (i = 0; FONT_CANDIDATES[i]; i++) {
        FILE *fp = fopen(FONT_CANDIDATES[i], "rb");

        if (fp) {
            fclose(fp);
            font_path = FONT_CANDIDATES[i];
            break;
        }
    }
    if (!font_path) {
        fprintf(stderr, "No monospace font found.\n");
        SDL_DestroyRenderer(g_app.renderer);
        SDL_DestroyWindow(g_app.window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    g_app.font_huge = TTF_OpenFont(font_path, 64);
    g_app.font_large = TTF_OpenFont(font_path, 26);
    g_app.font_med = TTF_OpenFont(font_path, 16);
    g_app.font_small = TTF_OpenFont(font_path, 12);
    if (!g_app.font_huge || !g_app.font_large || !g_app.font_med ||
        !g_app.font_small) {
        fprintf(stderr, "TTF_OpenFont: %s\n", TTF_GetError());
        return 1;
    }

    g_app.mode = MODE_VDC;
    g_app.autorange = true;
    g_app.hover_mode = -1;

    /* Serial port path is accepted for parity with the other instruments
     * but the protocol isn't implemented; demo mode is unconditional. */
    if (argc > 1)
        snprintf(g_app.serial_path, sizeof(g_app.serial_path), "%s", argv[1]);
    g_app.demo_mode = true;
    srand((unsigned int)time(NULL));

    while (running) {
        int win_w = 0, win_h = 0;
        Uint32 now;
        SDL_Event e;

        SDL_GetWindowSize(g_app.window, &win_w, &win_h);
        compute_layout(&g_app, win_w, win_h);

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE ||
                    e.key.keysym.sym == SDLK_q)
                    running = 0;
                else if (e.key.keysym.sym == SDLK_h)
                    g_app.hold = !g_app.hold;
                else if (e.key.keysym.sym == SDLK_r)
                    reset_stats(&g_app);
                else if (e.key.keysym.sym >= SDLK_1 &&
                         e.key.keysym.sym <= SDLK_6) {
                    DmmMode m = (DmmMode)(e.key.keysym.sym - SDLK_1);

                    if (m != g_app.mode) {
                        g_app.mode = m;
                        reset_stats(&g_app);
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                g_app.hover_mode = hit_mode_btn(&g_app, e.motion.x,
                                                e.motion.y);
                g_app.hover_btn = hit_other_btn(&g_app, e.motion.x,
                                                e.motion.y);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT)
                    on_click(&g_app, e.button.x, e.button.y);
                break;
            }
        }

        /* Sample at ~10 Hz. */
        now = SDL_GetTicks();
        if (now - last_sample > 100) {
            if (!g_app.hold) {
                g_app.value = sim_value(&g_app);
                update_stats(&g_app, g_app.value);
            }
            last_sample = now;
        }

        set_color(g_app.renderer, COL_BG_DARK);
        SDL_RenderClear(g_app.renderer);

        draw_header(g_app.renderer, win_w);
        draw_mode_bar(g_app.renderer, &g_app);
        {
            int vfd_x = MODE_BAR_W + PAD * 2;
            int vfd_y = HEADER_H + PAD;
            int vfd_w = win_w - vfd_x - STATS_W - PAD * 2;
            int vfd_h = win_h - HEADER_H - FOOTER_H - PAD * 2;

            if (vfd_w < 200)
                vfd_w = 200;
            draw_vfd_panel(g_app.renderer, &g_app, vfd_x, vfd_y, vfd_w, vfd_h);
            draw_stats_panel(g_app.renderer, &g_app, vfd_x + vfd_w + PAD,
                             vfd_y, STATS_W, vfd_h);
        }
        draw_footer(g_app.renderer, &g_app, win_w, win_h);

        SDL_RenderPresent(g_app.renderer);
        SDL_Delay(16);
    }

    TTF_CloseFont(g_app.font_huge);
    TTF_CloseFont(g_app.font_large);
    TTF_CloseFont(g_app.font_med);
    TTF_CloseFont(g_app.font_small);
    SDL_DestroyRenderer(g_app.renderer);
    SDL_DestroyWindow(g_app.window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
