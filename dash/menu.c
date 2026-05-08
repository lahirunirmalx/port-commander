#include "menu.h"

#include "render.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>

/* ------------------------------------------------------------------------- */
/* Geometry constants                                                        */
/* ------------------------------------------------------------------------- */

#define OPTION_H        52
#define OPTION_GAP      14
#define OPTION_STEP     (OPTION_H + OPTION_GAP)

/*
 * Arc parameters control how aggressively options bend away from the
 * selector. CURVE_RADIUS is the implied circle's radius; smaller values
 * curve more sharply. CURVE_MAX_OFFSET caps how far an off-screen
 * option can be pushed so distant rows don't fly across the window.
 */
#define CURVE_RADIUS      280.0f
#define CURVE_MAX_OFFSET   90.0f

/*
 * Spring stiffness for the scroll animation. Higher = snappier; lower
 * = softer. SPRING_K is the per-frame approach factor (essentially a
 * linear lerp); SPRING_EPS is the value below which we snap to target
 * to avoid sub-pixel drift.
 */
#define SPRING_K   0.22f
#define SPRING_EPS 0.5f

/* Header / footer reserved heights — must match dash/main.c so the
 * menu's clip region doesn't overlap the chrome. If the chrome ever
 * grows, expose these as init parameters. */
#define MENU_TOP_INSET    80
#define MENU_BOTTOM_INSET 60

/* ------------------------------------------------------------------------- */
/* Private helpers                                                           */
/* ------------------------------------------------------------------------- */

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/*
 * Curved offset: distance the option is pushed outward along the arc as
 * it moves vertically away from the selector. Mirrors get_curved_offset
 * in the smoothui example — clamped sqrt of (R² - dy²).
 */
static int curved_offset(float delta_y)
{
    float y = delta_y;
    float inside;
    float x;

    if (y > CURVE_RADIUS) y = CURVE_RADIUS;
    else if (y < -CURVE_RADIUS) y = -CURVE_RADIUS;
    inside = CURVE_RADIUS * CURVE_RADIUS - y * y;
    if (inside < 0.0f) inside = 0.0f;
    x = CURVE_RADIUS - sqrtf(inside);
    if (x > CURVE_MAX_OFFSET) x = CURVE_MAX_OFFSET;
    return (int)(x + 0.5f);
}

/*
 * Resolves the selector x/y and per-frame option width given the
 * current window size. Pulled out so layout and render share the same
 * geometry calculation.
 */
static void compute_geometry(int win_w, int win_h, int *out_selector_x,
                             int *out_selector_y, int *out_option_w)
{
    int margin_left = win_w / 12;
    int option_w;
    int selector_y;

    if (margin_left < 24)
        margin_left = 24;
    option_w = win_w - margin_left * 2 - (int)CURVE_MAX_OFFSET;
    if (option_w < 200)
        option_w = 200;
    selector_y = win_h / 2 - OPTION_H / 2;

    *out_selector_x = margin_left;
    *out_selector_y = selector_y;
    *out_option_w = option_w;
}

static void draw_curve_arc(SDL_Renderer *r, int cx, int cy, int radius,
                           int y_top, int y_bot, SDL_Color c)
{
    SDL_Point pts[129];
    int n = (int)(sizeof(pts) / sizeof(pts[0])) - 1;
    int count = 0;
    int i;

    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (i = 0; i <= n; i++) {
        float t = (float)i / (float)n * 2.0f * 3.14159265f;
        int px = cx + (int)(radius * cosf(t));
        int py = cy + (int)(radius * sinf(t));

        if (py < y_top || py > y_bot)
            continue;
        pts[count].x = px;
        pts[count].y = py;
        count++;
    }
    if (count >= 2)
        SDL_RenderDrawLines(r, pts, count);
}

static void draw_option(SDL_Renderer *r, TTF_Font *font_md, TTF_Font *font_sm,
                        const Tile *t, int is_selected)
{
    SDL_Color border = is_selected ? COL_ACCENT : COL_DIM;
    SDL_Color bg = COL_CARD;
    int title_y = t->rect.y + 8;
    int desc_y = title_y + 22;
    int x_text = t->rect.x + 16;
    int badge_w = 20;

    if (t->running_count > 0) {
        bg = COL_CARD_RUN;
        if (!is_selected)
            border = COL_GOOD;
    } else if (is_selected) {
        bg = COL_CARD_SEL;
    } else if (t->hover) {
        bg = COL_CARD_HOVER;
    }

    render_fill(r, t->rect, bg);
    render_outline(r, t->rect, border);

    if (t->title)
        render_text(r, font_md, t->title, x_text, title_y, COL_TEXT);
    if (t->desc)
        render_text(r, font_sm, t->desc, x_text, desc_y, COL_DIM);

    if (t->hotkey) {
        char hk[2] = { t->hotkey, 0 };
        SDL_Rect badge = { t->rect.x + t->rect.w - badge_w - 12,
                           t->rect.y + 8, badge_w, 18 };

        render_outline(r, badge, COL_DIM);
        render_text_centered(r, font_sm, hk, badge.x + badge.w / 2,
                             badge.y + 2, COL_DIM);
    }

    if (t->missing) {
        render_text(r, font_sm, "missing",
                    t->rect.x + t->rect.w - 80, desc_y, COL_DANGER);
    } else if (t->running_count > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "● %d", t->running_count);
        render_text(r, font_sm, buf, t->rect.x + t->rect.w - 80, desc_y,
                    COL_GOOD);
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void menu_init(Menu *m)
{
    if (!m)
        return;
    m->selected_idx = 0;
    m->scroll_y = 0.0f;
}

MenuAction menu_handle_event(Menu *m, const SDL_Event *e, Tile *tiles,
                             int n, int *out_index)
{
    if (!m || !e || !tiles || n <= 0)
        return MENU_NONE;

    if (e->type == SDL_KEYDOWN) {
        SDL_Keycode k;

        /* Skip OS-level auto-repeat: only act on the initial press.
         * Without this, holding a hotkey for ~300 ms launches the same
         * tool twice. */
        if (e->key.repeat)
            return MENU_NONE;
        k = e->key.keysym.sym;

        if (k == SDLK_ESCAPE || k == SDLK_q)
            return MENU_QUIT;

        if (k == SDLK_UP || k == SDLK_w || k == SDLK_k) {
            m->selected_idx = clampi(m->selected_idx - 1, 0, n - 1);
        } else if (k == SDLK_DOWN || k == SDLK_s || k == SDLK_j) {
            m->selected_idx = clampi(m->selected_idx + 1, 0, n - 1);
        } else if (k == SDLK_HOME) {
            m->selected_idx = 0;
        } else if (k == SDLK_END) {
            m->selected_idx = n - 1;
        } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER ||
                   k == SDLK_SPACE) {
            if (out_index)
                *out_index = m->selected_idx;
            return MENU_LAUNCH;
        } else if (k >= SDLK_SPACE && k <= SDLK_z) {
            /* Hotkey lookup. Reserved keys (q/w/s/k/j) have already been
             * handled above so they can't double as hotkeys; every other
             * printable character can. */
            int j;
            char ch = (char)k;

            for (j = 0; j < n; j++) {
                if (tiles[j].hotkey &&
                    tolower((unsigned char)tiles[j].hotkey) ==
                        tolower((unsigned char)ch)) {
                    m->selected_idx = j;
                    if (out_index)
                        *out_index = j;
                    return MENU_LAUNCH;
                }
            }
        }
        return MENU_NONE;
    }

    if (e->type == SDL_MOUSEWHEEL) {
        if (e->wheel.y > 0)
            m->selected_idx = clampi(m->selected_idx - 1, 0, n - 1);
        else if (e->wheel.y < 0)
            m->selected_idx = clampi(m->selected_idx + 1, 0, n - 1);
        return MENU_NONE;
    }

    if (e->type == SDL_MOUSEMOTION) {
        int j;
        for (j = 0; j < n; j++)
            tiles[j].hover = render_pt_in_rect(e->motion.x, e->motion.y,
                                               tiles[j].rect);
        return MENU_NONE;
    }

    if (e->type == SDL_MOUSEBUTTONDOWN &&
        e->button.button == SDL_BUTTON_LEFT) {
        int j;

        for (j = 0; j < n; j++) {
            if (render_pt_in_rect(e->button.x, e->button.y,
                                  tiles[j].rect)) {
                if (j == m->selected_idx) {
                    if (out_index)
                        *out_index = j;
                    return MENU_LAUNCH;
                }
                m->selected_idx = j;
                return MENU_NONE;
            }
        }
    }

    return MENU_NONE;
}

void menu_step(Menu *m)
{
    float target;
    float diff;

    if (!m)
        return;
    target = (float)m->selected_idx * (float)OPTION_STEP;
    diff = target - m->scroll_y;
    m->scroll_y += diff * SPRING_K;
    if (fabsf(diff) < SPRING_EPS)
        m->scroll_y = target;
}

void menu_layout(const Menu *m, Tile *tiles, int n, int win_w, int win_h)
{
    int selector_x;
    int selector_y;
    int option_w;
    int i;

    if (!m || !tiles)
        return;

    compute_geometry(win_w, win_h, &selector_x, &selector_y, &option_w);

    for (i = 0; i < n; i++) {
        float keyframe_y = (float)i * (float)OPTION_STEP;
        float delta_y = keyframe_y - m->scroll_y;
        int curved = curved_offset(delta_y);

        tiles[i].rect.x = selector_x + curved;
        tiles[i].rect.y = (int)(keyframe_y - m->scroll_y) + selector_y;
        tiles[i].rect.w = option_w;
        tiles[i].rect.h = OPTION_H;
    }
}

void menu_render(const Menu *m, SDL_Renderer *r, TTF_Font *font_md,
                 TTF_Font *font_sm, const Tile *tiles, int n,
                 int win_w, int win_h)
{
    int selector_x;
    int selector_y;
    int option_w;
    SDL_Rect clip;
    SDL_Rect sel;
    int i;

    if (!m || !r || !tiles)
        return;

    compute_geometry(win_w, win_h, &selector_x, &selector_y, &option_w);

    /* Curve guide arc, drawn behind the options. */
    {
        int arc_cx = selector_x + (int)CURVE_RADIUS;
        int arc_cy = selector_y + OPTION_H / 2;
        int clip_top = MENU_TOP_INSET;
        int clip_bot = win_h - MENU_BOTTOM_INSET;

        draw_curve_arc(r, arc_cx, arc_cy, (int)CURVE_RADIUS,
                       clip_top, clip_bot, COL_CURVE);
    }

    /* Options. Clip to the menu region so off-screen items don't spill
     * into header/footer chrome. */
    clip.x = 0;
    clip.y = MENU_TOP_INSET;
    clip.w = win_w;
    clip.h = win_h - MENU_TOP_INSET - MENU_BOTTOM_INSET;
    if (clip.h < 0) clip.h = 0;
    SDL_RenderSetClipRect(r, &clip);

    for (i = 0; i < n; i++) {
        if (tiles[i].rect.y + tiles[i].rect.h < clip.y) continue;
        if (tiles[i].rect.y > clip.y + clip.h) continue;
        draw_option(r, font_md, font_sm, &tiles[i], i == m->selected_idx);
    }
    SDL_RenderSetClipRect(r, NULL);

    /* Fixed selector outline at the screen's vertical centre. */
    sel.x = selector_x - 4;
    sel.y = selector_y - 4;
    sel.w = option_w + 8;
    sel.h = OPTION_H + 8;
    render_outline(r, sel, COL_ACCENT);
}
