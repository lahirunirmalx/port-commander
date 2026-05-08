/*
 * APcommander dashboard.
 *
 * Curved-menu launcher for the suite. Tools are laid out as a vertical
 * stack of options; a fixed-position selector sits at the screen's
 * vertical center and the option list scrolls past it. Each option's
 * horizontal position bends outward along a circular arc as it moves
 * away from the selector, mimicking the
 * fixed_selector_v_stack_curved_menu example from smoothui.
 *
 * Tile contents are loaded from a user-editable INI config — the
 * dashboard never has tile data baked into the binary. Search order:
 *   1. argv[1], if a path is given on the command line
 *   2. $APCOMMANDER_CONFIG environment variable
 *   3. $XDG_CONFIG_HOME/apcommander/tiles.conf (or ~/.config/...)
 *   4. <app_dir>/apcommander.conf (user override next to the binary)
 *   5. <app_dir>/apcommander.default.conf (shipped defaults — copied
 *      next to the binary at build time by CMakeLists.txt).
 * If none of those resolve, the dashboard fails to start with a clear
 * pointer at where to put a config.
 *
 * Platform-specific bits (executable directory, spawn/poll, exe suffix,
 * $PATH lookup) live in compat/. Adding Windows means filling in the
 * `_WIN32` branches there.
 */
#include "compat.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#  define PATH_MAX 1024
#endif

static const char *const FONT_CANDIDATES[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf",
    /* Common Windows path (when porting). */
    "C:/Windows/Fonts/consola.ttf",
    NULL,
};

static const SDL_Color COL_BG = { 26, 28, 34, 255 };
static const SDL_Color COL_CARD = { 48, 50, 62, 255 };
static const SDL_Color COL_CARD_HOVER = { 60, 86, 124, 255 };
static const SDL_Color COL_CARD_SEL = { 70, 110, 160, 255 };
static const SDL_Color COL_CARD_RUN = { 44, 70, 56, 255 };
static const SDL_Color COL_TEXT = { 230, 232, 238, 255 };
static const SDL_Color COL_DIM = { 150, 154, 168, 255 };
static const SDL_Color COL_ACCENT = { 100, 180, 255, 255 };
static const SDL_Color COL_GOOD = { 120, 200, 130, 255 };
static const SDL_Color COL_DANGER = { 200, 90, 90, 255 };
static const SDL_Color COL_CURVE = { 70, 74, 88, 255 };

#define MAX_TILES 64
#define MAX_CHILDREN 64
#define MAX_TILE_ARGS 31  /* + argv[0] + NULL = 33 entries in the argv array */

/* Curved-menu geometry. The arc is anchored to the right of the visible
 * option strip; options at the selector center sit at offset 0, and
 * deflect outward to CURVE_MAX_OFFSET as they scroll up or down. */
#define OPTION_H 52
#define OPTION_GAP 14
#define OPTION_STEP (OPTION_H + OPTION_GAP)
#define CURVE_RADIUS 280.0f
#define CURVE_MAX_OFFSET 90.0f

/*
 * Raw fields from one [section] of the config file, before we resolve
 * the exec path and tokenize args. Kept on the stack while parsing.
 */
typedef struct TileSpec {
    char title[128];
    char desc[192];
    char exec[PATH_MAX];
    char args[1024];
    char hotkey;
} TileSpec;

typedef struct Tile {
    SDL_Rect rect;       /* recomputed every frame from animated scroll */
    char *title;         /* owned (strdup) */
    char *desc;          /* owned (strdup), may be NULL */
    char exe_path[PATH_MAX];
    char **argv;         /* owned, NULL-terminated; argv[0] = strdup(exe_path) */
    int argc;
    int hover;
    int missing;        /* 1 if exe_path isn't executable */
    int running_count;  /* number of currently-alive children for this tile */
    Uint32 last_launch_ms;  /* SDL_GetTicks() at last launch — debounces fast
                             * re-presses (key auto-repeat, double-clicks
                             * after focus stealing). */
    char hotkey;        /* a single printable char shown in the corner; 0 = none */
} Tile;

/* How long after a launch a second launch of the same tile is dropped.
 * Long enough to swallow OS key-repeat (typically 250-500 ms) and a
 * stray double-click after the spawned window grabs focus, short enough
 * that intentionally relaunching a tool feels responsive. */
#define LAUNCH_DEBOUNCE_MS 350

typedef struct Child {
    CompatProc *proc;
    int tile_index;
} Child;

static Child g_children[MAX_CHILDREN];
static int g_num_children = 0;

/* ------------------------------------------------------------------------- */

static int pt_in_rect(int mx, int my, SDL_Rect r)
{
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static char *xstrdup(const char *s)
{
    size_t n;
    char *o;

    if (!s)
        return NULL;
    n = strlen(s);
    o = malloc(n + 1);
    if (!o)
        return NULL;
    memcpy(o, s, n + 1);
    return o;
}

static void draw_rect(SDL_Renderer *r, SDL_Rect rect, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rect);
}

static void draw_rect_outline(SDL_Renderer *r, SDL_Rect rect, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(r, &rect);
}

static void draw_text(SDL_Renderer *r, TTF_Font *font, const char *text, int x,
                      int y, SDL_Color fg)
{
    SDL_Surface *surf;
    SDL_Texture *tex;
    SDL_Rect dst;

    if (!text || !text[0])
        return;
    surf = TTF_RenderUTF8_Blended(font, text, fg);
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
                               const char *text, int x_center, int y,
                               SDL_Color fg)
{
    int tw = 0;
    int th = 0;

    if (!text || !text[0])
        return;
    if (TTF_SizeUTF8(font, text, &tw, &th) != 0)
        return;
    draw_text(r, font, text, x_center - tw / 2, y, fg);
}

/* ------------------------------------------------------------------------- */
/* Config loading                                                            */
/* ------------------------------------------------------------------------- */

static char *trim(char *s)
{
    char *e;

    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' ||
                     e[-1] == '\n'))
        *--e = '\0';
    return s;
}

/*
 * Whitespace-split tokenizer with double-quote support. Writes up to
 * `max` tokens (each strdup'd) into `out` and returns the token count.
 * No escape sequences inside quotes — kept deliberately small.
 */
static int tokenize_args(const char *line, char **out, int max)
{
    int n = 0;
    const char *p = line ? line : "";
    char buf[1024];

    while (*p && n < max) {
        size_t blen = 0;

        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && blen < sizeof(buf) - 1)
                buf[blen++] = *p++;
            if (*p == '"')
                p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && blen < sizeof(buf) - 1)
                buf[blen++] = *p++;
        }
        buf[blen] = '\0';
        out[n] = xstrdup(buf);
        if (!out[n])
            break;
        n++;
    }
    return n;
}

/*
 * Resolves a config `exec` value to an absolute path:
 *   /abs/path        → use as-is
 *   ~/foo            → $HOME/foo
 *   contains '/'     → relative to app_dir
 *   bare name        → app_dir/name first, then $PATH
 *
 * Writes result to `out`. Returns 0 if the resolved path is executable,
 * -1 otherwise (out is still set to the best guess so the missing-tile
 * UI has something to display).
 */
static int resolve_exec(const char *app_dir, const char *exec, char *out,
                        size_t outsz)
{
    if (!exec || !exec[0] || !out || outsz == 0)
        return -1;

    if (exec[0] == '~' && exec[1] == '/') {
        const char *home = getenv("HOME");

        if (home && home[0]) {
            snprintf(out, outsz, "%s%s", home, exec + 1);
            return compat_can_execute(out) ? 0 : -1;
        }
    }
    if (exec[0] == '/') {
        snprintf(out, outsz, "%s", exec);
        return compat_can_execute(out) ? 0 : -1;
    }
    if (strchr(exec, '/')) {
        snprintf(out, outsz, "%s/%s", app_dir, exec);
        return compat_can_execute(out) ? 0 : -1;
    }

    /* Bare name: try app_dir first (sibling tool), then $PATH. */
    snprintf(out, outsz, "%s/%s%s", app_dir, exec, compat_exe_suffix());
    if (compat_can_execute(out))
        return 0;
    if (compat_path_lookup(exec, out, outsz) == 0)
        return 0;
    /* Couldn't resolve — leave the app_dir guess so the user sees the
     * intended location. */
    snprintf(out, outsz, "%s/%s%s", app_dir, exec, compat_exe_suffix());
    return -1;
}

/*
 * Builds a Tile from a parsed TileSpec: resolves the exec path,
 * tokenizes the args, copies title/desc. Returns 0 on success and
 * leaves the Tile partially populated on failure (so missing-binary
 * tiles still appear with their title visible).
 */
static int finalize_tile(Tile *t, const TileSpec *spec, const char *app_dir)
{
    char *arg_tokens[MAX_TILE_ARGS];
    int arg_count = 0;
    int i;

    memset(t, 0, sizeof(*t));
    t->title = xstrdup(spec->title);
    t->desc = spec->desc[0] ? xstrdup(spec->desc) : NULL;
    t->hotkey = spec->hotkey;

    if (resolve_exec(app_dir, spec->exec, t->exe_path,
                     sizeof(t->exe_path)) != 0)
        t->missing = 1;

    arg_count = tokenize_args(spec->args, arg_tokens, MAX_TILE_ARGS);

    /* argv layout: [exe_path, parsed args..., NULL]. argv[0] is the path
     * by convention (what the child sees as argv[0]). */
    t->argv = calloc((size_t)arg_count + 2, sizeof(char *));
    if (!t->argv) {
        for (i = 0; i < arg_count; i++)
            free(arg_tokens[i]);
        free(t->title);
        free(t->desc);
        memset(t, 0, sizeof(*t));
        return -1;
    }
    t->argv[0] = xstrdup(t->exe_path);
    for (i = 0; i < arg_count; i++)
        t->argv[i + 1] = arg_tokens[i];
    t->argv[arg_count + 1] = NULL;
    t->argc = arg_count + 1;
    return 0;
}

static void free_tile(Tile *t)
{
    int i;

    if (!t)
        return;
    free(t->title);
    free(t->desc);
    if (t->argv) {
        for (i = 0; t->argv[i]; i++)
            free(t->argv[i]);
        free(t->argv);
    }
    memset(t, 0, sizeof(*t));
}

/*
 * Reads an INI-style config file into TileSpec entries. Returns the
 * number of entries on success, -1 on file open failure (errmsg set).
 *
 * Format is intentionally minimal:
 *   - Lines starting with '#' or ';' (after whitespace) are comments.
 *   - "[section]" begins a new tile entry. The section name itself is
 *     not displayed; it just acts as a record separator.
 *   - "key = value" inside a section. Recognized keys: title, desc,
 *     exec, args, hotkey. Unknown keys are ignored.
 *   - Values run to the end of the line; no inline-comment stripping
 *     (so "exec = /opt/foo#bar" works as a path).
 *
 * A section is committed (turned into a TileSpec) only if it has both
 * title and exec — incomplete entries are silently skipped.
 */
static int load_config(const char *path, TileSpec *out, int max,
                       char *errmsg, size_t errsz)
{
    FILE *fp;
    char line[2048];
    TileSpec cur;
    int in_section = 0;
    int n = 0;

    if (errmsg && errsz)
        errmsg[0] = '\0';
    fp = fopen(path, "r");
    if (!fp) {
        if (errmsg)
            snprintf(errmsg, errsz, "open %s: %s", path, strerror(errno));
        return -1;
    }
    memset(&cur, 0, sizeof(cur));

    while (fgets(line, sizeof(line), fp)) {
        char *s = trim(line);
        char *eq;
        char *key;
        char *val;
        char *ke;

        if (!*s || *s == '#' || *s == ';')
            continue;

        if (*s == '[') {
            /* Commit the previous section if it had the required fields. */
            if (in_section && cur.title[0] && cur.exec[0] && n < max)
                out[n++] = cur;
            memset(&cur, 0, sizeof(cur));
            in_section = 1;
            continue;
        }
        if (!in_section)
            continue;

        eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        key = s;
        val = eq + 1;

        ke = key + strlen(key);
        while (ke > key && (ke[-1] == ' ' || ke[-1] == '\t'))
            *--ke = '\0';
        while (*val == ' ' || *val == '\t')
            val++;

        if (strcmp(key, "title") == 0) {
            snprintf(cur.title, sizeof(cur.title), "%s", val);
        } else if (strcmp(key, "desc") == 0) {
            snprintf(cur.desc, sizeof(cur.desc), "%s", val);
        } else if (strcmp(key, "exec") == 0) {
            snprintf(cur.exec, sizeof(cur.exec), "%s", val);
        } else if (strcmp(key, "args") == 0) {
            snprintf(cur.args, sizeof(cur.args), "%s", val);
        } else if (strcmp(key, "hotkey") == 0 && val[0]) {
            /* Single printable char; ignore anything else. */
            unsigned char c = (unsigned char)val[0];
            if (isprint(c))
                cur.hotkey = (char)c;
        }
    }

    if (in_section && cur.title[0] && cur.exec[0] && n < max)
        out[n++] = cur;

    fclose(fp);
    return n;
}

/*
 * Walks the config search chain. Writes the chosen path to `out_path`
 * (for status display) and returns the number of TileSpecs loaded, or
 * -1 if no config file could be found at any of the search locations.
 * `cli_path` is argv[1] if provided, else NULL.
 *
 * The dashboard does not carry an in-binary fallback: every tile comes
 * from a file. The shipped `apcommander.default.conf` (copied next to
 * the binary at build time) is the lowest-priority entry in the chain.
 */
static int find_and_load_config(const char *cli_path, TileSpec *specs,
                                int max, char *out_path, size_t out_path_sz)
{
    char path[PATH_MAX];
    char errmsg[256];
    const char *home;
    const char *xdg;
    char app_dir[PATH_MAX - 64];
    int n;

    out_path[0] = '\0';

    if (cli_path && cli_path[0]) {
        n = load_config(cli_path, specs, max, errmsg, sizeof(errmsg));
        if (n >= 0) {
            snprintf(out_path, out_path_sz, "%s", cli_path);
            return n;
        }
        fprintf(stderr, "config: %s\n", errmsg);
    }

    {
        const char *env = getenv("APCOMMANDER_CONFIG");
        if (env && env[0]) {
            n = load_config(env, specs, max, errmsg, sizeof(errmsg));
            if (n >= 0) {
                snprintf(out_path, out_path_sz, "%s", env);
                return n;
            }
            fprintf(stderr, "config: %s\n", errmsg);
        }
    }

    xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(path, sizeof(path), "%s/apcommander/tiles.conf", xdg);
        n = load_config(path, specs, max, errmsg, sizeof(errmsg));
        if (n >= 0) {
            snprintf(out_path, out_path_sz, "%s", path);
            return n;
        }
    } else {
        home = getenv("HOME");
        if (home && home[0]) {
            snprintf(path, sizeof(path), "%s/.config/apcommander/tiles.conf",
                     home);
            n = load_config(path, specs, max, errmsg, sizeof(errmsg));
            if (n >= 0) {
                snprintf(out_path, out_path_sz, "%s", path);
                return n;
            }
        }
    }

    if (compat_app_dir(app_dir, sizeof(app_dir)) == 0) {
        snprintf(path, sizeof(path), "%s/apcommander.conf", app_dir);
        n = load_config(path, specs, max, errmsg, sizeof(errmsg));
        if (n >= 0) {
            snprintf(out_path, out_path_sz, "%s", path);
            return n;
        }
        snprintf(path, sizeof(path), "%s/apcommander.default.conf", app_dir);
        n = load_config(path, specs, max, errmsg, sizeof(errmsg));
        if (n >= 0) {
            snprintf(out_path, out_path_sz, "%s", path);
            return n;
        }
    }

    return -1;
}

/* ------------------------------------------------------------------------- */
/* Curved-menu layout & rendering                                            */
/* ------------------------------------------------------------------------- */

/* Curved offset: distance the option is pushed outward along the arc as
 * it moves vertically away from the selector. Mirrors get_curved_offset
 * in the smoothui example — clamped sqrt of (R² - dy²). */
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

/* Lay out the option rects given current animated scroll. Each option
 * has a fixed "keyframe" y (i * OPTION_STEP); the rendered y is offset
 * by the scroll so the selected option lands at the selector. */
static void layout_curved(int win_w, int win_h, Tile *tiles, int n,
                          float scroll_y, int *out_selector_x,
                          int *out_selector_y, int *out_option_w)
{
    int margin_left;
    int option_w;
    int selector_y;
    int i;

    /* Selector is left-of-center so the curve has room to bend rightward. */
    margin_left = win_w / 12;
    if (margin_left < 24) margin_left = 24;
    option_w = win_w - margin_left * 2 - (int)CURVE_MAX_OFFSET;
    if (option_w < 200) option_w = 200;

    selector_y = win_h / 2 - OPTION_H / 2;

    *out_selector_x = margin_left;
    *out_selector_y = selector_y;
    *out_option_w = option_w;

    for (i = 0; i < n; i++) {
        float keyframe_y = (float)i * (float)OPTION_STEP;
        float delta_y = keyframe_y - scroll_y;
        int curved = curved_offset(delta_y);

        tiles[i].rect.x = margin_left + curved;
        tiles[i].rect.y = (int)(keyframe_y - scroll_y) + selector_y;
        tiles[i].rect.w = option_w;
        tiles[i].rect.h = OPTION_H;
    }
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

    draw_rect(r, t->rect, bg);
    draw_rect_outline(r, t->rect, border);

    if (t->title)
        draw_text(r, font_md, t->title, x_text, title_y, COL_TEXT);
    if (t->desc)
        draw_text(r, font_sm, t->desc, x_text, desc_y, COL_DIM);

    if (t->hotkey) {
        char hk[2] = { t->hotkey, 0 };
        SDL_Rect badge = { t->rect.x + t->rect.w - badge_w - 12,
                           t->rect.y + 8, badge_w, 18 };

        draw_rect_outline(r, badge, COL_DIM);
        draw_text_centered(r, font_sm, hk, badge.x + badge.w / 2,
                           badge.y + 2, COL_DIM);
    }

    if (t->missing) {
        draw_text(r, font_sm, "missing",
                  t->rect.x + t->rect.w - 80, desc_y, COL_DANGER);
    } else if (t->running_count > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "● %d", t->running_count);
        draw_text(r, font_sm, buf, t->rect.x + t->rect.w - 80, desc_y,
                  COL_GOOD);
    }
}

/* ------------------------------------------------------------------------- */
/* Spawn / reap                                                              */
/* ------------------------------------------------------------------------- */

/*
 * Spawns a tool without blocking. The dashboard window stays visible and
 * the user can pick another option to launch a second instance. Returns
 * 0 on success, -1 on failure (status_msg set).
 */
static int launch_tile(Tile *tiles, int idx, char *status_msg, size_t status_sz)
{
    Tile *t = &tiles[idx];
    CompatProc *p;
    Uint32 now = SDL_GetTicks();

    if (status_msg && status_sz)
        status_msg[0] = '\0';

    /* Per-tile debounce: SDL emits KEYDOWN for both the initial press
     * and OS auto-repeat (~250–500 ms in), and a launched window stealing
     * focus can prompt the user to "click again" almost instantly. Both
     * paths funnel through here; drop a second launch within the
     * debounce window. SDL_GetTicks wraps after ~49 days, but the
     * subtraction is well-defined (Uint32 modular arithmetic) so the
     * comparison still works correctly across the wrap. */
    if (t->last_launch_ms != 0 &&
        (Uint32)(now - t->last_launch_ms) < (Uint32)LAUNCH_DEBOUNCE_MS)
        return 0;

    /* Re-check at click time — closes the TOCTOU window if the binary was
     * removed, swapped, or chmod'd between dashboard startup and now. */
    t->missing = !compat_can_execute(t->exe_path);
    if (t->missing) {
        if (status_msg)
            snprintf(status_msg, status_sz, "Not found: %s", t->exe_path);
        return -1;
    }
    if (g_num_children >= MAX_CHILDREN) {
        if (status_msg)
            snprintf(status_msg, status_sz,
                     "Already running %d tools — close one first.",
                     MAX_CHILDREN);
        return -1;
    }

    p = compat_spawn(t->exe_path, t->argv, status_msg, status_sz);
    if (!p) {
        /* Spawn failed; mark missing so the tile reflects current state. */
        t->missing = !compat_can_execute(t->exe_path);
        return -1;
    }

    g_children[g_num_children].proc = p;
    g_children[g_num_children].tile_index = idx;
    g_num_children++;
    t->running_count++;
    t->last_launch_ms = now ? now : 1;  /* keep 0 reserved for "never" */
    return 0;
}

/*
 * Reaps any children that have exited since the last call. Updates the
 * owning tile's running_count. Called every frame.
 *
 * A poll error (-1) is treated as terminal: keep one bad handle around
 * across many frames and on Windows the leaked HANDLE accumulates fast.
 * Better to drop the entry, free the handle, and surface the count drop.
 */
static void reap_children(Tile *tiles, int num_tiles)
{
    int i = 0;

    while (i < g_num_children) {
        int code = 0;
        int r = compat_proc_poll(g_children[i].proc, &code);

        if (r != 0) { /* exited (1) or errored (-1) */
            int idx = g_children[i].tile_index;

            if (idx >= 0 && idx < num_tiles && tiles[idx].running_count > 0)
                tiles[idx].running_count--;
            compat_proc_free(g_children[i].proc);
            g_children[i] = g_children[g_num_children - 1];
            g_num_children--;
        } else {
            i++;
        }
    }
}

static int total_running(void)
{
    return g_num_children;
}

/* ------------------------------------------------------------------------- */

static int sdl_setup(SDL_Window **win, SDL_Renderer **r)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    *win = SDL_CreateWindow("APcommander", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, 720, 660,
                            SDL_WINDOW_RESIZABLE);
    if (!*win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    *r = SDL_CreateRenderer(*win, -1, SDL_RENDERER_ACCELERATED);
    if (!*r) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(*win);
        SDL_Quit();
        return -1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        SDL_DestroyRenderer(*r);
        SDL_DestroyWindow(*win);
        SDL_Quit();
        return -1;
    }
    return 0;
}

static const char *find_font(void)
{
    int i;

    for (i = 0; FONT_CANDIDATES[i]; i++) {
        FILE *fp = fopen(FONT_CANDIDATES[i], "rb");

        if (fp) {
            fclose(fp);
            return FONT_CANDIDATES[i];
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    SDL_Window *win = NULL;
    SDL_Renderer *r = NULL;
    TTF_Font *font_lg = NULL;
    TTF_Font *font_md = NULL;
    TTF_Font *font_sm = NULL;
    char app_dir[PATH_MAX - 64];
    char status[512];
    char config_path[PATH_MAX];
    const char *font_path;
    TileSpec specs[MAX_TILES];
    Tile tiles[MAX_TILES];
    int num_tiles = 0;
    int running = 1;
    int selected_idx = 0;
    float scroll_y = 0.0f;
    int i;

    compat_app_dir(app_dir, sizeof(app_dir));
    status[0] = '\0';

    {
        const char *cli = (argc > 1) ? argv[1] : NULL;
        int n;

        n = find_and_load_config(cli, specs, MAX_TILES, config_path,
                                 sizeof(config_path));
        if (n < 0) {
            fprintf(stderr,
                "No launcher config found. Tried (in order):\n"
                "  argv[1], $APCOMMANDER_CONFIG,\n"
                "  $XDG_CONFIG_HOME/apcommander/tiles.conf "
                "(or ~/.config/apcommander/tiles.conf),\n"
                "  %s/apcommander.conf,\n"
                "  %s/apcommander.default.conf\n"
                "Place a config at one of those paths, or run "
                "`cmake --build build` to install the shipped "
                "default next to the binary.\n", app_dir, app_dir);
            return 1;
        }
        for (i = 0; i < n && num_tiles < MAX_TILES; i++) {
            if (finalize_tile(&tiles[num_tiles], &specs[i], app_dir) == 0)
                num_tiles++;
        }
    }

    if (num_tiles == 0) {
        fprintf(stderr, "Config %s had no usable [section] entries. "
                        "Each tile needs at least 'title' and 'exec' set.\n",
                        config_path);
        return 1;
    }

    if (sdl_setup(&win, &r) != 0) {
        for (i = 0; i < num_tiles; i++)
            free_tile(&tiles[i]);
        return 1;
    }

    font_path = find_font();
    if (!font_path) {
        fprintf(stderr, "No font found. Install fonts-dejavu-core "
                        "(Debian/Ubuntu) or equivalent.\n");
        TTF_Quit();
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        for (i = 0; i < num_tiles; i++)
            free_tile(&tiles[i]);
        return 1;
    }
    font_lg = TTF_OpenFont(font_path, 22);
    font_md = TTF_OpenFont(font_path, 16);
    font_sm = TTF_OpenFont(font_path, 12);
    if (!font_lg || !font_md || !font_sm) {
        fprintf(stderr, "TTF_OpenFont: %s\n", TTF_GetError());
        if (font_lg) TTF_CloseFont(font_lg);
        if (font_md) TTF_CloseFont(font_md);
        if (font_sm) TTF_CloseFont(font_sm);
        TTF_Quit();
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        for (i = 0; i < num_tiles; i++)
            free_tile(&tiles[i]);
        return 1;
    }

    while (running) {
        int win_w = 0;
        int win_h = 0;
        int selector_x = 0;
        int selector_y = 0;
        int option_w = 0;
        SDL_Event e;

        reap_children(tiles, num_tiles);

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
                break;
            }
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k;

                /* Skip OS-level auto-repeat: only act on the initial
                 * press. Without this, holding a hotkey for ~300 ms
                 * launches the same tool twice. The debounce in
                 * launch_tile is a backstop for cases the repeat flag
                 * doesn't cover (e.g. spurious double-clicks after a
                 * spawned window steals focus). */
                if (e.key.repeat)
                    continue;
                k = e.key.keysym.sym;

                if (k == SDLK_ESCAPE || k == SDLK_q) {
                    running = 0;
                    break;
                }
                if (k == SDLK_UP || k == SDLK_w || k == SDLK_k) {
                    selected_idx = clampi(selected_idx - 1, 0, num_tiles - 1);
                } else if (k == SDLK_DOWN || k == SDLK_s || k == SDLK_j) {
                    selected_idx = clampi(selected_idx + 1, 0, num_tiles - 1);
                } else if (k == SDLK_HOME) {
                    selected_idx = 0;
                } else if (k == SDLK_END) {
                    selected_idx = num_tiles - 1;
                } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER ||
                           k == SDLK_SPACE) {
                    if (selected_idx >= 0 && selected_idx < num_tiles)
                        (void)launch_tile(tiles, selected_idx, status,
                                          sizeof(status));
                } else if (k >= SDLK_SPACE && k <= SDLK_z) {
                    /* Hotkey lookup. Reserved keys (q, w, s, k, j) have
                     * already been handled above so they can't double as
                     * hotkeys; every other printable character can. */
                    int j;
                    char ch = (char)k;

                    for (j = 0; j < num_tiles; j++) {
                        if (tiles[j].hotkey &&
                            tolower((unsigned char)tiles[j].hotkey) ==
                                tolower((unsigned char)ch)) {
                            selected_idx = j;
                            (void)launch_tile(tiles, j, status,
                                              sizeof(status));
                            break;
                        }
                    }
                }
            } else if (e.type == SDL_MOUSEWHEEL) {
                if (e.wheel.y > 0)
                    selected_idx = clampi(selected_idx - 1, 0, num_tiles - 1);
                else if (e.wheel.y < 0)
                    selected_idx = clampi(selected_idx + 1, 0, num_tiles - 1);
            } else if (e.type == SDL_MOUSEMOTION) {
                int j;
                for (j = 0; j < num_tiles; j++)
                    tiles[j].hover = pt_in_rect(e.motion.x, e.motion.y,
                                                tiles[j].rect);
            } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                       e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x;
                int my = e.button.y;
                int j;

                for (j = 0; j < num_tiles; j++) {
                    if (pt_in_rect(mx, my, tiles[j].rect)) {
                        if (j == selected_idx) {
                            (void)launch_tile(tiles, j, status,
                                              sizeof(status));
                        } else {
                            selected_idx = j;
                        }
                        break;
                    }
                }
            }
        }

        /* Animate scroll toward the selected option (simple ease — the
         * smoothui example uses a spring; this is the C-with-no-deps
         * approximation that still feels alive). */
        {
            float target = (float)selected_idx * (float)OPTION_STEP;
            float diff = target - scroll_y;

            scroll_y += diff * 0.22f;
            if (fabsf(diff) < 0.5f)
                scroll_y = target;
        }

        SDL_GetWindowSize(win, &win_w, &win_h);
        layout_curved(win_w, win_h, tiles, num_tiles, scroll_y,
                      &selector_x, &selector_y, &option_w);

        SDL_SetRenderDrawColor(r, COL_BG.r, COL_BG.g, COL_BG.b, 255);
        SDL_RenderClear(r);

        /* Header. */
        draw_text_centered(r, font_lg, "APcommander", win_w / 2, 24,
                           COL_ACCENT);
        draw_text_centered(r, font_sm, "Local system utilities", win_w / 2,
                           56, COL_DIM);

        /* Curve guide arc, drawn behind the options. */
        {
            int arc_cx = selector_x + (int)CURVE_RADIUS;
            int arc_cy = selector_y + OPTION_H / 2;
            int clip_top = 80;
            int clip_bot = win_h - 60;

            draw_curve_arc(r, arc_cx, arc_cy, (int)CURVE_RADIUS,
                           clip_top, clip_bot, COL_CURVE);
        }

        /* Options. Clip to the menu region so off-screen items don't
         * spill into header/footer. */
        {
            SDL_Rect clip;
            clip.x = 0;
            clip.y = 80;
            clip.w = win_w;
            clip.h = win_h - 140;
            if (clip.h < 0) clip.h = 0;
            SDL_RenderSetClipRect(r, &clip);

            for (i = 0; i < num_tiles; i++) {
                if (tiles[i].rect.y + tiles[i].rect.h < clip.y) continue;
                if (tiles[i].rect.y > clip.y + clip.h) continue;
                draw_option(r, font_md, font_sm, &tiles[i],
                            i == selected_idx);
            }
            SDL_RenderSetClipRect(r, NULL);
        }

        /* Fixed selector outline at the screen's vertical center. */
        {
            SDL_Rect sel;
            sel.x = selector_x - 4;
            sel.y = selector_y - 4;
            sel.w = option_w + 8;
            sel.h = OPTION_H + 8;
            draw_rect_outline(r, sel, COL_ACCENT);
        }

        /* Footer. */
        if (status[0]) {
            draw_text_centered(r, font_sm, status, win_w / 2, win_h - 44,
                               COL_DANGER);
        } else if (total_running() > 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%d tool%s running",
                     total_running(), total_running() == 1 ? "" : "s");
            draw_text_centered(r, font_sm, buf, win_w / 2, win_h - 44,
                               COL_GOOD);
        } else if (config_path[0]) {
            char buf[PATH_MAX + 16];
            snprintf(buf, sizeof(buf), "config: %s", config_path);
            draw_text_centered(r, font_sm, buf, win_w / 2, win_h - 44,
                               COL_DIM);
        }
        draw_text_centered(r, font_sm,
                           "↑/↓ navigate  ·  Enter launch  ·  hotkeys  ·  Esc quit",
                           win_w / 2, win_h - 22, COL_DIM);

        SDL_RenderPresent(r);
        SDL_Delay(16);
    }

    /* Detach any still-running children — leave them alive so the user can
     * keep using them after closing the dashboard. */
    for (i = 0; i < g_num_children; i++)
        compat_proc_free(g_children[i].proc);
    g_num_children = 0;

    for (i = 0; i < num_tiles; i++)
        free_tile(&tiles[i]);

    TTF_CloseFont(font_lg);
    TTF_CloseFont(font_md);
    TTF_CloseFont(font_sm);
    TTF_Quit();
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
