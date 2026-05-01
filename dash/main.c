#define _GNU_SOURCE
#include <SDL.h>
#include <SDL_ttf.h>

#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *const FONT_CANDIDATES[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf",
    NULL,
};

static const SDL_Color COL_BG = { 26, 28, 34, 255 };
static const SDL_Color COL_CARD = { 48, 50, 62, 255 };
static const SDL_Color COL_CARD_HOVER = { 60, 86, 124, 255 };
static const SDL_Color COL_TEXT = { 230, 232, 238, 255 };
static const SDL_Color COL_DIM = { 150, 154, 168, 255 };
static const SDL_Color COL_ACCENT = { 100, 180, 255, 255 };
static const SDL_Color COL_DANGER = { 200, 90, 90, 255 };

#define MAX_TILES 12
#define TILE_W 220
#define TILE_H 140
#define TILE_GAP 16
#define GRID_COLS 4

typedef struct Tile {
    SDL_Rect rect;
    const char *title;
    const char *desc;
    char exe_path[PATH_MAX];
    int hover;
    int missing; /* 1 if exe_path isn't executable */
    char hotkey; /* '1'..'9' shown in corner; 0 = none */
} Tile;

static int pt_in_rect(int mx, int my, SDL_Rect r)
{
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
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

/*
 * Sets out to the directory containing /proc/self/exe, e.g. /home/x/build.
 * Falls back to "." on failure.
 */
static void self_dir(char *out, size_t outsz)
{
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    char *d;

    if (n <= 0) {
        snprintf(out, outsz, ".");
        return;
    }
    buf[n] = '\0';
    d = dirname(buf); /* mutates buf */
    snprintf(out, outsz, "%s", d);
}

/*
 * Forks and execvps the given binary path with no extra args. Hides the
 * dashboard window while the child is running so the user only sees the
 * tool. Returns the child's exit code, or -1 on failure (errmsg set).
 */
static int run_tool(SDL_Window *win, const char *path, char *errmsg,
                    size_t errsz)
{
    pid_t pid;
    int status;

    if (errmsg && errsz)
        errmsg[0] = '\0';

    if (access(path, X_OK) != 0) {
        if (errmsg)
            snprintf(errmsg, errsz, "Not executable: %s (%s)", path,
                     strerror(errno));
        return -1;
    }

    SDL_HideWindow(win);

    pid = fork();
    if (pid < 0) {
        if (errmsg)
            snprintf(errmsg, errsz, "fork failed: %s", strerror(errno));
        SDL_ShowWindow(win);
        return -1;
    }
    if (pid == 0) {
        char *argv[2] = { (char *)path, NULL };
        execvp(path, argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        if (errmsg)
            snprintf(errmsg, errsz, "waitpid: %s", strerror(errno));
        SDL_ShowWindow(win);
        return -1;
    }
    SDL_ShowWindow(win);
    SDL_RaiseWindow(win);
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 127 && errmsg)
            snprintf(errmsg, errsz, "Failed to exec %s", path);
        return code;
    }
    return -1;
}

static void register_tile(Tile *tiles, int *n, const char *title,
                          const char *desc, const char *dir,
                          const char *exe_name, char hotkey)
{
    Tile *t;

    if (*n >= MAX_TILES)
        return;
    t = &tiles[(*n)++];
    memset(t, 0, sizeof(*t));
    t->title = title;
    t->desc = desc;
    t->hotkey = hotkey;
    snprintf(t->exe_path, sizeof(t->exe_path), "%s/%s", dir, exe_name);
    t->missing = (access(t->exe_path, X_OK) != 0);
}

static void layout_tiles(int win_w, int win_h, Tile *tiles, int n)
{
    int rows = (n + GRID_COLS - 1) / GRID_COLS;
    int grid_w = GRID_COLS * TILE_W + (GRID_COLS - 1) * TILE_GAP;
    int grid_h = rows * TILE_H + (rows - 1) * TILE_GAP;
    /* Reserve space for the title/subtitle (~96 px) above and the footer
     * (~48 px) below so the grid sits in the available middle band. */
    int y_top = 96;
    int y_bot = win_h - 48;
    int avail_h = y_bot - y_top;
    int x0 = (win_w - grid_w) / 2;
    int y0 = y_top + (avail_h - grid_h) / 2;
    int i;

    if (x0 < TILE_GAP)
        x0 = TILE_GAP;
    if (y0 < y_top)
        y0 = y_top;

    for (i = 0; i < n; i++) {
        int row = i / GRID_COLS;
        int col = i % GRID_COLS;

        tiles[i].rect.x = x0 + col * (TILE_W + TILE_GAP);
        tiles[i].rect.y = y0 + row * (TILE_H + TILE_GAP);
        tiles[i].rect.w = TILE_W;
        tiles[i].rect.h = TILE_H;
    }
}

static void draw_tile(SDL_Renderer *r, TTF_Font *font_md, TTF_Font *font_sm,
                      const Tile *t)
{
    SDL_Color border = t->hover ? COL_ACCENT : COL_DIM;
    SDL_Color bg = t->hover ? COL_CARD_HOVER : COL_CARD;
    int cx = t->rect.x + t->rect.w / 2;
    int title_y = t->rect.y + 22;
    int desc_y = title_y + 32;

    draw_rect(r, t->rect, bg);
    draw_rect_outline(r, t->rect, border);

    /* Hotkey badge in the top-right corner */
    if (t->hotkey) {
        char hk[2] = { t->hotkey, 0 };
        SDL_Rect badge = { t->rect.x + t->rect.w - 22,
                           t->rect.y + 6, 16, 16 };
        draw_rect_outline(r, badge, COL_DIM);
        draw_text_centered(r, font_sm, hk, badge.x + badge.w / 2,
                           badge.y + 1, COL_DIM);
    }

    draw_text_centered(r, font_md, t->title, cx, title_y, COL_TEXT);
    draw_text_centered(r, font_sm, t->desc, cx, desc_y, COL_DIM);

    if (t->missing) {
        draw_text_centered(r, font_sm, "(binary not found)", cx,
                           t->rect.y + t->rect.h - 26, COL_DANGER);
    }
}

int main(int argc, char *argv[])
{
    SDL_Window *win = NULL;
    SDL_Renderer *r = NULL;
    TTF_Font *font_lg = NULL;
    TTF_Font *font_md = NULL;
    TTF_Font *font_sm = NULL;
    char dir[PATH_MAX - 64];
    char status[512];
    const char *font_path = NULL;
    Tile tiles[MAX_TILES];
    int num_tiles = 0;
    int running = 1;
    int i;

    (void)argc;
    (void)argv;

    self_dir(dir, sizeof(dir));
    status[0] = '\0';

    register_tile(tiles, &num_tiles, "Port Commander",
                  "Inspect open TCP/UDP sockets", dir, "portcommander", '1');
    register_tile(tiles, &num_tiles, "Wi-Fi Commander",
                  "Manage Wi-Fi & hotspot", dir, "wificommander", '2');
    register_tile(tiles, &num_tiles, "ADC Monitor",
                  "8-channel 24-bit ADC scope", dir, "adc_gui", '3');
    register_tile(tiles, &num_tiles, "PSU Dual",
                  "Dual-channel power supply", dir, "psu_gui", '4');
    register_tile(tiles, &num_tiles, "PSU Single",
                  "Single-channel power supply", dir, "psu_gui_single", '5');
    register_tile(tiles, &num_tiles, "PSU Toolbar Dual",
                  "Compact dual-channel strip", dir, "psu_gui_toolbar", '6');
    register_tile(tiles, &num_tiles, "PSU Toolbar Single",
                  "Compact single-channel strip", dir,
                  "psu_gui_toolbar_single", '7');

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("APcommander", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, 1000, 580,
                           SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!r) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    for (i = 0; FONT_CANDIDATES[i]; i++) {
        FILE *fp = fopen(FONT_CANDIDATES[i], "r");
        if (fp) {
            fclose(fp);
            font_path = FONT_CANDIDATES[i];
            break;
        }
    }
    if (!font_path) {
        fprintf(stderr, "No font found. Install fonts-dejavu-core.\n");
        TTF_Quit();
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    font_lg = TTF_OpenFont(font_path, 22);
    font_md = TTF_OpenFont(font_path, 16);
    font_sm = TTF_OpenFont(font_path, 12);
    if (!font_lg || !font_md || !font_sm) {
        fprintf(stderr, "TTF_OpenFont: %s\n", TTF_GetError());
        if (font_lg)
            TTF_CloseFont(font_lg);
        if (font_md)
            TTF_CloseFont(font_md);
        if (font_sm)
            TTF_CloseFont(font_sm);
        TTF_Quit();
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    while (running) {
        int win_w = 0;
        int win_h = 0;
        SDL_Event e;

        SDL_GetWindowSize(win, &win_w, &win_h);
        layout_tiles(win_w, win_h, tiles, num_tiles);

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
                break;
            }
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;

                if (k == SDLK_ESCAPE || k == SDLK_q) {
                    running = 0;
                    break;
                }
                if (k >= SDLK_1 && k <= SDLK_9) {
                    int idx = k - SDLK_1;
                    if (idx < num_tiles && !tiles[idx].missing) {
                        (void)run_tool(win, tiles[idx].exe_path, status,
                                       sizeof(status));
                    }
                }
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
                    if (pt_in_rect(mx, my, tiles[j].rect) &&
                        !tiles[j].missing) {
                        (void)run_tool(win, tiles[j].exe_path, status,
                                       sizeof(status));
                        break;
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(r, COL_BG.r, COL_BG.g, COL_BG.b, 255);
        SDL_RenderClear(r);

        draw_text_centered(r, font_lg, "APcommander", win_w / 2, 28,
                           COL_ACCENT);
        draw_text_centered(r, font_sm, "Local system utilities", win_w / 2,
                           62, COL_DIM);

        for (i = 0; i < num_tiles; i++)
            draw_tile(r, font_md, font_sm, &tiles[i]);

        if (status[0]) {
            draw_text_centered(r, font_sm, status, win_w / 2, win_h - 44,
                               COL_DANGER);
        }
        draw_text_centered(r, font_sm,
                           "Click a tile or press 1–7 to launch.   Esc to quit.",
                           win_w / 2, win_h - 22, COL_DIM);

        SDL_RenderPresent(r);
        SDL_Delay(16);
    }

    TTF_CloseFont(font_lg);
    TTF_CloseFont(font_md);
    TTF_CloseFont(font_sm);
    TTF_Quit();
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
