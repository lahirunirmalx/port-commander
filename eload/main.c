/*
 * DC Electronic Load GUI.
 *
 * Standalone SDL2 application styled after the imported PSU/ADC GUIs:
 * dark panel theme, VFD-style readouts, cyan accent, common widget sizes.
 * Demo mode simulates load behaviour from the active mode + setpoint with
 * a simple Ohm's-law model so the readouts look alive without hardware.
 *
 * Modes: CC (constant current), CV (constant voltage), CR (constant
 * resistance), CP (constant power). The setpoint field re-units itself
 * automatically when mode changes.
 */
#define _POSIX_C_SOURCE 200809L

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vfd.h"

#define WIN_W 820
#define WIN_H 520
#define HEADER_H 44
#define FOOTER_H 28
#define MODE_BAR_W 80
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
static const Color COL_ACCENT __attribute__((unused)) = { 0, 180, 220, 255 };
static const Color COL_SUCCESS = { 50, 205, 100, 255 };
static const Color COL_WARNING = { 255, 180, 0, 255 };
static const Color COL_ERROR = { 220, 60, 60, 255 };
static const Color COL_VFD_BG = { 8, 18, 12, 255 };
static const Color COL_VFD_ON = { 0, 255, 120, 255 };
static const Color COL_VFD_OFF = { 0, 60, 35, 255 };
static const Color COL_BTN_NORMAL = { 50, 50, 55, 255 };
static const Color COL_BTN_HOVER = { 65, 65, 72, 255 };
static const Color COL_BTN_ACTIVE = { 0, 140, 170, 255 };
static const Color COL_BTN_DANGER = { 140, 40, 40, 255 };
static const Color COL_BTN_GO = { 40, 130, 70, 255 };

static const char *const FONT_CANDIDATES[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    NULL,
};

typedef enum {
    MODE_CC = 0, /* Constant Current */
    MODE_CV,     /* Constant Voltage */
    MODE_CR,     /* Constant Resistance */
    MODE_CP,     /* Constant Power */
    MODE_COUNT,
} EloadMode;

static const char *const MODE_NAMES[MODE_COUNT] = { "CC", "CV", "CR", "CP" };
static const char *const MODE_FULL[MODE_COUNT] = { "Constant Current",
                                                   "Constant Voltage",
                                                   "Constant Resistance",
                                                   "Constant Power" };
static const char *const MODE_UNITS[MODE_COUNT] = { "A", "V", "Ω", "W" };

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font_huge;
    TTF_Font *font_large;
    TTF_Font *font_med;
    TTF_Font *font_small;

    /* State */
    EloadMode mode;
    double setpoint[MODE_COUNT];
    bool input_on;
    /* Protections */
    double ovp; /* over-voltage protection (V) */
    double ocp; /* over-current protection (A) */
    double opp; /* over-power protection (W) */
    bool tripped;
    char trip_reason[64];

    /* Live measured values */
    double v, i, p, r;
    double sim_phase;

    /* Setpoint editing state */
    bool editing;
    char edit_buf[32];
    int edit_pos;

    /* Hit boxes */
    SDL_Rect mode_hits[MODE_COUNT];
    SDL_Rect setpt_hit;
    SDL_Rect input_hit;
    SDL_Rect setpt_up_hit, setpt_dn_hit;
    SDL_Rect ovp_hit, ocp_hit, opp_hit;
    int hover_mode;

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
                      int y, Color fg)
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
    dst.x = x;
    dst.y = y;
    dst.w = surf->w;
    dst.h = surf->h;
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
    draw_text(r, font, text, cx - tw / 2, cy - th / 2, fg);
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

/*
 * Trivial demo simulator: assume a 12V source with 0.05Ω equivalent
 * series resistance. Compute a load current that satisfies the active
 * setpoint, then derive V/I/P/R. Includes light noise so the readouts
 * tick.
 */
static void simulate(App *a)
{
    const double Vsrc = 12.0;
    const double Rsrc = 0.05;
    double sp = a->setpoint[a->mode];
    double load_R;
    double i_req;
    double v_meas;
    double noise;

    if (!a->input_on || a->tripped) {
        a->v = Vsrc;
        a->i = 0.0;
        a->p = 0.0;
        a->r = 0.0;
        return;
    }

    switch (a->mode) {
    case MODE_CC:
        i_req = sp;
        v_meas = Vsrc - i_req * Rsrc;
        if (v_meas < 0)
            v_meas = 0;
        load_R = (i_req > 1e-9) ? v_meas / i_req : 0;
        break;
    case MODE_CV:
        v_meas = sp;
        if (v_meas > Vsrc)
            v_meas = Vsrc;
        i_req = (Vsrc - v_meas) / Rsrc;
        if (i_req < 0)
            i_req = 0;
        load_R = (i_req > 1e-9) ? v_meas / i_req : 0;
        break;
    case MODE_CR:
        load_R = sp > 0 ? sp : 1.0;
        i_req = Vsrc / (load_R + Rsrc);
        v_meas = Vsrc - i_req * Rsrc;
        break;
    case MODE_CP: {
        double a_q = -Rsrc;
        double b_q = Vsrc;
        double c_q = -sp;
        double disc = b_q * b_q - 4 * a_q * c_q;

        if (disc < 0)
            disc = 0;
        i_req = (-b_q + sqrt(disc)) / (2 * a_q);
        if (i_req < 0 || !isfinite(i_req))
            i_req = 0;
        v_meas = Vsrc - i_req * Rsrc;
        load_R = (i_req > 1e-9) ? v_meas / i_req : 0;
        break;
    }
    default:
        v_meas = Vsrc;
        i_req = 0;
        load_R = 0;
        break;
    }

    /* light noise so the display ticks */
    a->sim_phase += 0.07;
    noise = 0.002 * sin(a->sim_phase) +
            0.001 * (((double)rand() / RAND_MAX) - 0.5);

    a->v = v_meas + noise * v_meas;
    a->i = i_req + noise * i_req;
    a->p = a->v * a->i;
    a->r = load_R;

    /* Protection trips */
    if (a->ovp > 0 && a->v > a->ovp) {
        a->tripped = true;
        a->input_on = false;
        snprintf(a->trip_reason, sizeof(a->trip_reason), "OVP %.2f V", a->v);
    } else if (a->ocp > 0 && a->i > a->ocp) {
        a->tripped = true;
        a->input_on = false;
        snprintf(a->trip_reason, sizeof(a->trip_reason), "OCP %.2f A", a->i);
    } else if (a->opp > 0 && a->p > a->opp) {
        a->tripped = true;
        a->input_on = false;
        snprintf(a->trip_reason, sizeof(a->trip_reason), "OPP %.2f W", a->p);
    }
}

/* ------------------------------------------------------------------------- */

static void compute_layout(App *a, int win_w, int win_h)
{
    int top_y = HEADER_H + PAD;
    int bot_y = win_h - FOOTER_H - PAD;
    int avail_h = bot_y - top_y;
    int btn_h = 56;
    int i;

    /* Mode bar (left) */
    for (i = 0; i < MODE_COUNT; i++) {
        a->mode_hits[i].x = PAD;
        a->mode_hits[i].y = top_y + i * (btn_h + 6);
        a->mode_hits[i].w = MODE_BAR_W;
        a->mode_hits[i].h = btn_h;
    }

    /* INPUT ON/OFF master in the header area, top-right */
    a->input_hit.w = 130;
    a->input_hit.h = 28;
    a->input_hit.x = win_w - PAD - a->input_hit.w;
    a->input_hit.y = (HEADER_H - a->input_hit.h) / 2;

    /* Setpoint row sits along the top of the panel area */
    {
        int sx = PAD * 2 + MODE_BAR_W;
        int sy = top_y;

        a->setpt_hit.x = sx + 80;
        a->setpt_hit.y = sy;
        a->setpt_hit.w = 220;
        a->setpt_hit.h = 36;

        a->setpt_dn_hit.x = a->setpt_hit.x + a->setpt_hit.w + 8;
        a->setpt_dn_hit.y = sy;
        a->setpt_dn_hit.w = 36;
        a->setpt_dn_hit.h = 36;

        a->setpt_up_hit.x = a->setpt_dn_hit.x + a->setpt_dn_hit.w + 4;
        a->setpt_up_hit.y = sy;
        a->setpt_up_hit.w = 36;
        a->setpt_up_hit.h = 36;
    }

    /* Protection row sits along the footer */
    {
        int fy = win_h - FOOTER_H + 2;

        a->ovp_hit.x = PAD + MODE_BAR_W + 60;
        a->ovp_hit.y = fy;
        a->ovp_hit.w = 90;
        a->ovp_hit.h = FOOTER_H - 4;

        a->ocp_hit.x = a->ovp_hit.x + a->ovp_hit.w + 8;
        a->ocp_hit.y = fy;
        a->ocp_hit.w = 90;
        a->ocp_hit.h = FOOTER_H - 4;

        a->opp_hit.x = a->ocp_hit.x + a->ocp_hit.w + 8;
        a->opp_hit.y = fy;
        a->opp_hit.w = 90;
        a->opp_hit.h = FOOTER_H - 4;
    }

    (void)avail_h;
}

static void draw_header(SDL_Renderer *r, App *a, int win_w)
{
    set_color(r, COL_HEADER);
    fill_rect(r, 0, 0, win_w, HEADER_H);
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, HEADER_H - 1, win_w, HEADER_H - 1);

    draw_text(r, a->font_med, "DC ELECTRONIC LOAD", PAD, 12, COL_TEXT);
    draw_text(r, a->font_small, MODE_FULL[a->mode], PAD + 220, 16, COL_LABEL);

    {
        Color c = a->demo_mode ? COL_WARNING : COL_SUCCESS;
        const char *label = a->demo_mode ? "DEMO" : "ONLINE";
        int tw = 0;

        TTF_SizeUTF8(a->font_small, label, &tw, NULL);
        draw_text(r, a->font_small, label,
                  a->input_hit.x - tw - 18, 16, c);
        draw_led(r, a->input_hit.x - 10, HEADER_H / 2, 5, true, c);
    }

    /* Master INPUT button */
    {
        SDL_Rect b = a->input_hit;
        Color bg = a->input_on ? COL_BTN_GO : COL_BTN_DANGER;
        char buf[32];

        snprintf(buf, sizeof(buf), "INPUT  %s", a->input_on ? "ON" : "OFF");
        set_color(r, bg);
        SDL_RenderFillRect(r, &b);
        set_color(r, COL_BORDER_LIGHT);
        SDL_RenderDrawRect(r, &b);
        draw_text_centered(r, a->font_med, buf, b.x + b.w / 2, b.y + b.h / 2,
                           COL_TEXT);
    }
}

static void draw_mode_bar(SDL_Renderer *r, App *a)
{
    int i;

    for (i = 0; i < MODE_COUNT; i++) {
        SDL_Rect b = a->mode_hits[i];
        Color bg = COL_BTN_NORMAL;

        if (a->mode == (EloadMode)i)
            bg = COL_BTN_ACTIVE;
        else if (a->hover_mode == i)
            bg = COL_BTN_HOVER;

        set_color(r, bg);
        SDL_RenderFillRect(r, &b);
        set_color(r, COL_BORDER_LIGHT);
        SDL_RenderDrawRect(r, &b);
        draw_text_centered(r, a->font_large, MODE_NAMES[i],
                           b.x + b.w / 2, b.y + b.h / 2, COL_TEXT);
    }
}

static void draw_setpoint_row(SDL_Renderer *r, App *a)
{
    char buf[64];
    SDL_Rect b = a->setpt_hit;
    VfdColor on_col = { COL_VFD_ON.r, COL_VFD_ON.g, COL_VFD_ON.b,
                        COL_VFD_ON.a };
    VfdColor off_col = { COL_VFD_OFF.r, COL_VFD_OFF.g, COL_VFD_OFF.b, 255 };

    draw_text(r, a->font_med, "SETPOINT", b.x - 76, b.y + 10, COL_LABEL);

    set_color(r, COL_VFD_BG);
    SDL_RenderFillRect(r, &b);
    set_color(r, COL_VFD_OFF);
    SDL_RenderDrawRect(r, &b);

    if (a->editing) {
        /* When typing, render the partial input in TTF — the edit cursor is
         * easier to read than dot-matrix in this case. */
        snprintf(buf, sizeof(buf), "%s_  %s", a->edit_buf,
                 MODE_UNITS[a->mode]);
        draw_text(r, a->font_large, buf, b.x + 12, b.y + 6, COL_VFD_ON);
    } else {
        int dot_size, dot_gap, char_gap;
        int chars;
        int unit_tw = 0, unit_th = 0;
        int num_w;

        snprintf(buf, sizeof(buf), "%7.3f", a->setpoint[a->mode]);
        TTF_SizeUTF8(a->font_med, MODE_UNITS[a->mode], &unit_tw, &unit_th);

        chars = (int)strlen(buf);
        dot_size = vfd_fit_dot_size(chars, b.w - 16 - unit_tw, b.h - 6, 1, 3);
        dot_gap = 1;
        char_gap = dot_size * 2;
        num_w = vfd_measure(buf, dot_size, dot_gap, char_gap);

        {
            int dot_h = 7 * (dot_size * 2 + dot_gap);
            int total_w = num_w + 12 + unit_tw;
            int start_x = b.x + (b.w - total_w) / 2;
            int start_y = b.y + (b.h - dot_h) / 2;

            if (start_x < b.x + 8)
                start_x = b.x + 8;
            vfd_draw_number(r, start_x, start_y, buf, dot_size, dot_gap,
                            char_gap, on_col, off_col, true);
            draw_text(r, a->font_med, MODE_UNITS[a->mode],
                      start_x + num_w + 12,
                      start_y + dot_h / 2 - unit_th / 2 + 2, COL_VFD_ON);
        }
    }

    /* up/down arrows */
    {
        SDL_Rect d = a->setpt_dn_hit;
        SDL_Rect u = a->setpt_up_hit;

        set_color(r, COL_BTN_NORMAL);
        SDL_RenderFillRect(r, &d);
        SDL_RenderFillRect(r, &u);
        set_color(r, COL_BORDER_LIGHT);
        SDL_RenderDrawRect(r, &d);
        SDL_RenderDrawRect(r, &u);
        draw_text_centered(r, a->font_large, "-", d.x + d.w / 2, d.y + d.h / 2,
                           COL_TEXT);
        draw_text_centered(r, a->font_large, "+", u.x + u.w / 2, u.y + u.h / 2,
                           COL_TEXT);
    }
}

static void draw_readout(SDL_Renderer *r, App *a, int x, int y, int w, int h,
                         const char *label, double value, const char *unit,
                         Color value_col)
{
    char buf[64];
    VfdColor on_col = { value_col.r, value_col.g, value_col.b, 255 };
    VfdColor off_col = { COL_VFD_OFF.r, COL_VFD_OFF.g, COL_VFD_OFF.b, 255 };

    set_color(r, COL_BG_PANEL);
    fill_rect(r, x, y, w, h);
    set_color(r, COL_BORDER);
    stroke_rect(r, x, y, w, h);

    draw_text(r, a->font_med, label, x + 12, y + 10, COL_LABEL);

    /* VFD strip with dot-matrix readout */
    {
        int sx = x + 12;
        int sy = y + 36;
        int sw = w - 24;
        int sh = h - 50;
        int dot_size, dot_gap, char_gap;
        int dot_h;
        int num_w;
        int chars;
        int unit_tw = 0, unit_th = 0;

        set_color(r, COL_VFD_BG);
        fill_rect(r, sx, sy, sw, sh);
        set_color(r, COL_VFD_OFF);
        stroke_rect(r, sx, sy, sw, sh);

        snprintf(buf, sizeof(buf), "%7.3f", value);
        TTF_SizeUTF8(a->font_large, unit, &unit_tw, &unit_th);

        chars = (int)strlen(buf);
        /* Auto-size to fit both width and height with margin for the unit
         * label on the right. */
        dot_size = vfd_fit_dot_size(chars, sw - 20 - unit_tw, sh - 12, 2, 5);
        dot_gap = 1;
        char_gap = dot_size * 2;
        dot_h = 7 * (dot_size * 2 + dot_gap);
        num_w = vfd_measure(buf, dot_size, dot_gap, char_gap);

        {
            int total_w = num_w + 12 + unit_tw;
            int start_x = sx + (sw - total_w) / 2;
            int start_y = sy + (sh - dot_h) / 2;

            if (start_x < sx + 8)
                start_x = sx + 8;
            vfd_draw_number(r, start_x, start_y, buf, dot_size, dot_gap,
                            char_gap, on_col, off_col, true);
            draw_text(r, a->font_large, unit, start_x + num_w + 12,
                      start_y + dot_h / 2 - unit_th / 2 + 2, value_col);
        }
    }
}

static void draw_protection_row(SDL_Renderer *r, App *a, int win_w, int win_h)
{
    char buf[64];

    set_color(r, COL_HEADER);
    fill_rect(r, 0, win_h - FOOTER_H, win_w, FOOTER_H);
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, win_h - FOOTER_H, win_w, win_h - FOOTER_H);

    draw_text(r, a->font_small, "PROT", PAD + MODE_BAR_W + 16,
              win_h - FOOTER_H + 8, COL_LABEL);

    /* Each protection slot is a small clickable rect that bumps the limit. */
    {
        SDL_Rect rects[3] = { a->ovp_hit, a->ocp_hit, a->opp_hit };
        const char *names[3] = { "OVP", "OCP", "OPP" };
        double vals[3] = { a->ovp, a->ocp, a->opp };
        const char *units[3] = { "V", "A", "W" };
        int i;

        for (i = 0; i < 3; i++) {
            set_color(r, COL_BTN_NORMAL);
            SDL_RenderFillRect(r, &rects[i]);
            set_color(r, COL_BORDER_LIGHT);
            SDL_RenderDrawRect(r, &rects[i]);
            if (vals[i] > 0)
                snprintf(buf, sizeof(buf), "%s %.1f%s", names[i], vals[i],
                         units[i]);
            else
                snprintf(buf, sizeof(buf), "%s --", names[i]);
            draw_text_centered(r, a->font_small, buf,
                               rects[i].x + rects[i].w / 2,
                               rects[i].y + rects[i].h / 2,
                               vals[i] > 0 ? COL_TEXT : COL_TEXT_DIM);
        }
    }

    if (a->tripped) {
        char trip[160];

        snprintf(trip, sizeof(trip), "TRIPPED: %s — click INPUT to clear",
                 a->trip_reason);
        draw_text(r, a->font_small, trip, win_w - PAD - 360,
                  win_h - FOOTER_H + 8, COL_ERROR);
    } else {
        const char *hint =
            a->demo_mode
                ? "Demo: 12V source, 0.05Ω series. Use +/- to nudge setpoint."
                : "Connected.";

        draw_text(r, a->font_small, hint, win_w - PAD - 460,
                  win_h - FOOTER_H + 8, COL_TEXT_DIM);
    }
}

/* ------------------------------------------------------------------------- */

static double setpoint_step(EloadMode m)
{
    switch (m) {
    case MODE_CC:
        return 0.1;   /* 100 mA */
    case MODE_CV:
        return 0.5;   /* 0.5 V */
    case MODE_CR:
        return 1.0;   /* 1 Ω */
    case MODE_CP:
        return 1.0;   /* 1 W */
    default:
        return 0.1;
    }
}

static void clamp_setpoint(App *a)
{
    double *sp = &a->setpoint[a->mode];

    if (*sp < 0)
        *sp = 0;
    if (a->mode == MODE_CV && *sp > 60)
        *sp = 60;
    if (a->mode == MODE_CC && *sp > 30)
        *sp = 30;
    if (a->mode == MODE_CP && *sp > 200)
        *sp = 200;
    if (a->mode == MODE_CR && *sp > 10000)
        *sp = 10000;
}

static void on_click(App *a, int x, int y)
{
    int i;

    if (point_in_rect(x, y, a->input_hit)) {
        if (a->tripped) {
            a->tripped = false;
            a->trip_reason[0] = '\0';
        }
        a->input_on = !a->input_on;
        return;
    }
    for (i = 0; i < MODE_COUNT; i++) {
        if (point_in_rect(x, y, a->mode_hits[i])) {
            a->mode = (EloadMode)i;
            a->editing = false;
            return;
        }
    }
    if (point_in_rect(x, y, a->setpt_dn_hit)) {
        a->setpoint[a->mode] -= setpoint_step(a->mode);
        clamp_setpoint(a);
        return;
    }
    if (point_in_rect(x, y, a->setpt_up_hit)) {
        a->setpoint[a->mode] += setpoint_step(a->mode);
        clamp_setpoint(a);
        return;
    }
    if (point_in_rect(x, y, a->setpt_hit)) {
        a->editing = true;
        a->edit_pos = 0;
        a->edit_buf[0] = '\0';
        SDL_StartTextInput();
        return;
    }
    /* Click anywhere else cancels editing */
    if (a->editing) {
        a->editing = false;
        SDL_StopTextInput();
    }
    /* Protection slots: each click cycles through a few presets. */
    if (point_in_rect(x, y, a->ovp_hit)) {
        a->ovp = (a->ovp == 0) ? 14.0 : (a->ovp == 14.0 ? 30.0 : 0.0);
    } else if (point_in_rect(x, y, a->ocp_hit)) {
        a->ocp = (a->ocp == 0) ? 5.0 : (a->ocp == 5.0 ? 10.0 : 0.0);
    } else if (point_in_rect(x, y, a->opp_hit)) {
        a->opp = (a->opp == 0) ? 50.0 : (a->opp == 50.0 ? 100.0 : 0.0);
    }
}

static void apply_edit(App *a)
{
    if (a->edit_pos > 0) {
        char *endp;
        double v = strtod(a->edit_buf, &endp);

        /* Reject unparseable input ("--", ".", garbage) and non-finite
         * values (NaN, ±inf from "1e9999"). atof would silently
         * substitute 0/inf/NaN and propagate NaN through every
         * downstream simulator readout, with all comparison-based
         * protections (OVP/OCP/OPP) returning false on NaN. */
        if (endp != a->edit_buf && isfinite(v)) {
            a->setpoint[a->mode] = v;
            clamp_setpoint(a);
        }
    }
    a->editing = false;
    SDL_StopTextInput();
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
        SDL_CreateWindow("DC Electronic Load", SDL_WINDOWPOS_CENTERED,
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

    g_app.font_huge = TTF_OpenFont(font_path, 36);
    g_app.font_large = TTF_OpenFont(font_path, 22);
    g_app.font_med = TTF_OpenFont(font_path, 16);
    g_app.font_small = TTF_OpenFont(font_path, 12);
    if (!g_app.font_huge || !g_app.font_large || !g_app.font_med ||
        !g_app.font_small) {
        fprintf(stderr, "TTF_OpenFont: %s\n", TTF_GetError());
        if (g_app.font_huge)  TTF_CloseFont(g_app.font_huge);
        if (g_app.font_large) TTF_CloseFont(g_app.font_large);
        if (g_app.font_med)   TTF_CloseFont(g_app.font_med);
        if (g_app.font_small) TTF_CloseFont(g_app.font_small);
        SDL_DestroyRenderer(g_app.renderer);
        SDL_DestroyWindow(g_app.window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    g_app.mode = MODE_CC;
    g_app.setpoint[MODE_CC] = 1.0;  /* 1 A */
    g_app.setpoint[MODE_CV] = 5.0;  /* 5 V */
    g_app.setpoint[MODE_CR] = 10.0; /* 10 Ω */
    g_app.setpoint[MODE_CP] = 10.0; /* 10 W */
    g_app.hover_mode = -1;

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
            case SDL_KEYDOWN: {
                SDL_Keycode k = e.key.keysym.sym;

                if (g_app.editing) {
                    if (k == SDLK_ESCAPE) {
                        g_app.editing = false;
                        SDL_StopTextInput();
                    } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                        apply_edit(&g_app);
                    } else if (k == SDLK_BACKSPACE) {
                        if (g_app.edit_pos > 0) {
                            g_app.edit_pos--;
                            g_app.edit_buf[g_app.edit_pos] = '\0';
                        }
                    }
                    break;
                }
                if (k == SDLK_ESCAPE || k == SDLK_q) {
                    running = 0;
                } else if (k == SDLK_SPACE) {
                    if (g_app.tripped) {
                        g_app.tripped = false;
                        g_app.trip_reason[0] = '\0';
                    }
                    g_app.input_on = !g_app.input_on;
                } else if (k >= SDLK_1 && k < SDLK_1 + MODE_COUNT) {
                    /* Index-bounded against MODE_COUNT so a future
                     * change to the mode list can't write an out-of-
                     * range value into g_app.mode (which feeds several
                     * MODE_COUNT-sized lookup tables). */
                    g_app.mode = (EloadMode)(k - SDLK_1);
                } else if (k == SDLK_UP) {
                    g_app.setpoint[g_app.mode] += setpoint_step(g_app.mode);
                    clamp_setpoint(&g_app);
                } else if (k == SDLK_DOWN) {
                    g_app.setpoint[g_app.mode] -= setpoint_step(g_app.mode);
                    clamp_setpoint(&g_app);
                }
                break;
            }
            case SDL_TEXTINPUT:
                if (g_app.editing) {
                    const char *t = e.text.text;
                    size_t n = strlen(t);
                    size_t cap = sizeof(g_app.edit_buf) - 1;

                    while (n--) {
                        char c = *t++;
                        int accept = 0;

                        /* Allow digits anywhere; '-' only at position 0;
                         * '.' only if not already present. Without this
                         * filter a user could type "--..3.14" — strtod
                         * stops at the first invalid char, so it would
                         * still parse, but the visible buffer becomes
                         * confusing. */
                        if (c >= '0' && c <= '9') {
                            accept = 1;
                        } else if (c == '-' && g_app.edit_pos == 0) {
                            accept = 1;
                        } else if (c == '.' &&
                                   memchr(g_app.edit_buf, '.',
                                          (size_t)g_app.edit_pos) == NULL) {
                            accept = 1;
                        }
                        if (accept) {
                            if ((size_t)g_app.edit_pos < cap) {
                                g_app.edit_buf[g_app.edit_pos++] = c;
                                g_app.edit_buf[g_app.edit_pos] = '\0';
                            }
                        }
                    }
                }
                break;
            case SDL_MOUSEMOTION: {
                int j;
                g_app.hover_mode = -1;
                for (j = 0; j < MODE_COUNT; j++)
                    if (point_in_rect(e.motion.x, e.motion.y,
                                      g_app.mode_hits[j])) {
                        g_app.hover_mode = j;
                        break;
                    }
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT)
                    on_click(&g_app, e.button.x, e.button.y);
                break;
            }
        }

        now = SDL_GetTicks();
        if (now - last_sample > 100) {
            simulate(&g_app);
            last_sample = now;
        }

        set_color(g_app.renderer, COL_BG_DARK);
        SDL_RenderClear(g_app.renderer);

        draw_header(g_app.renderer, &g_app, win_w);
        draw_mode_bar(g_app.renderer, &g_app);
        draw_setpoint_row(g_app.renderer, &g_app);

        /* 2x2 grid of readouts */
        {
            int gx = PAD * 2 + MODE_BAR_W;
            int gy = HEADER_H + PAD + 50; /* below setpoint row */
            int gw = win_w - gx - PAD;
            int gh = win_h - gy - FOOTER_H - PAD;
            int cw = (gw - PAD) / 2;
            int ch = (gh - PAD) / 2;
            Color iv = g_app.input_on && !g_app.tripped ? COL_VFD_ON
                                                        : COL_VFD_OFF;

            draw_readout(g_app.renderer, &g_app, gx, gy, cw, ch,
                         "VOLTAGE", g_app.v, "V", iv);
            draw_readout(g_app.renderer, &g_app, gx + cw + PAD, gy, cw, ch,
                         "CURRENT", g_app.i, "A", iv);
            draw_readout(g_app.renderer, &g_app, gx, gy + ch + PAD, cw, ch,
                         "POWER", g_app.p, "W", iv);
            draw_readout(g_app.renderer, &g_app, gx + cw + PAD,
                         gy + ch + PAD, cw, ch, "RESISTANCE", g_app.r, "Ω",
                         iv);
        }

        draw_protection_row(g_app.renderer, &g_app, win_w, win_h);

        SDL_RenderPresent(g_app.renderer);
        SDL_Delay(16);
    }

    if (g_app.editing)
        SDL_StopTextInput();
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
