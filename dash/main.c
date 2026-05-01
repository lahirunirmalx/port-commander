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

typedef struct Tile {
    SDL_Rect rect;
    const char *title;
    const char *desc;
    const char *exe_path;
    int hover;
    int missing; /* 1 if exe_path isn't executable */
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

static void layout_tiles(int win_w, int win_h, Tile tiles[2])
{
    int gap = 24;
    int tile_w = (win_w - gap * 3) / 2;
    int tile_h = 200;
    int tiles_y = (win_h - tile_h) / 2 + 20;

    if (tile_w < 220)
        tile_w = 220;

    tiles[0].rect.x = gap;
    tiles[0].rect.y = tiles_y;
    tiles[0].rect.w = tile_w;
    tiles[0].rect.h = tile_h;

    tiles[1].rect.x = gap * 2 + tile_w;
    tiles[1].rect.y = tiles_y;
    tiles[1].rect.w = tile_w;
    tiles[1].rect.h = tile_h;
}

static void draw_tile(SDL_Renderer *r, TTF_Font *font_lg, TTF_Font *font_sm,
                      const Tile *t)
{
    SDL_Color border = t->hover ? COL_ACCENT : COL_DIM;
    SDL_Color bg = t->hover ? COL_CARD_HOVER : COL_CARD;
    int cx = t->rect.x + t->rect.w / 2;
    int title_y = t->rect.y + 56;
    int desc_y = title_y + 36;

    draw_rect(r, t->rect, bg);
    draw_rect_outline(r, t->rect, border);

    draw_text_centered(r, font_lg, t->title, cx, title_y, COL_TEXT);
    draw_text_centered(r, font_sm, t->desc, cx, desc_y, COL_DIM);

    if (t->missing) {
        draw_text_centered(r, font_sm, "(binary not found)", cx,
                           t->rect.y + t->rect.h - 30, COL_DANGER);
    }
}

int main(int argc, char *argv[])
{
    SDL_Window *win = NULL;
    SDL_Renderer *r = NULL;
    TTF_Font *font_lg = NULL;
    TTF_Font *font_sm = NULL;
    /* dir leaves headroom so PATH_MAX-sized port_path/wifi_path can always
     * hold "<dir>/wificommander" without truncation. */
    char dir[PATH_MAX - 64];
    char port_path[PATH_MAX];
    char wifi_path[PATH_MAX];
    char status[512];
    const char *font_path = NULL;
    Tile tiles[2];
    int running = 1;
    int i;

    (void)argc;
    (void)argv;

    self_dir(dir, sizeof(dir));
    snprintf(port_path, sizeof(port_path), "%s/portcommander", dir);
    snprintf(wifi_path, sizeof(wifi_path), "%s/wificommander", dir);
    status[0] = '\0';

    memset(tiles, 0, sizeof(tiles));
    tiles[0].title = "Port Commander";
    tiles[0].desc = "Inspect open TCP/UDP sockets";
    tiles[0].exe_path = port_path;
    tiles[0].missing = (access(port_path, X_OK) != 0);
    tiles[1].title = "Wi-Fi Commander";
    tiles[1].desc = "Manage Wi-Fi & hotspot";
    tiles[1].exe_path = wifi_path;
    tiles[1].missing = (access(wifi_path, X_OK) != 0);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("APcommander", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, 720, 480,
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
    font_sm = TTF_OpenFont(font_path, 13);
    if (!font_lg || !font_sm) {
        fprintf(stderr, "TTF_OpenFont: %s\n", TTF_GetError());
        if (font_lg)
            TTF_CloseFont(font_lg);
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
        layout_tiles(win_w, win_h, tiles);

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
                break;
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE ||
                    e.key.keysym.sym == SDLK_q) {
                    running = 0;
                    break;
                }
                if (e.key.keysym.sym == SDLK_1 && !tiles[0].missing) {
                    (void)run_tool(win, port_path, status, sizeof(status));
                } else if (e.key.keysym.sym == SDLK_2 && !tiles[1].missing) {
                    (void)run_tool(win, wifi_path, status, sizeof(status));
                }
            } else if (e.type == SDL_MOUSEMOTION) {
                tiles[0].hover = pt_in_rect(e.motion.x, e.motion.y,
                                            tiles[0].rect);
                tiles[1].hover = pt_in_rect(e.motion.x, e.motion.y,
                                            tiles[1].rect);
            } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                       e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x;
                int my = e.button.y;
                if (pt_in_rect(mx, my, tiles[0].rect) && !tiles[0].missing) {
                    (void)run_tool(win, port_path, status, sizeof(status));
                } else if (pt_in_rect(mx, my, tiles[1].rect) &&
                           !tiles[1].missing) {
                    (void)run_tool(win, wifi_path, status, sizeof(status));
                }
            }
        }

        SDL_SetRenderDrawColor(r, COL_BG.r, COL_BG.g, COL_BG.b, 255);
        SDL_RenderClear(r);

        draw_text_centered(r, font_lg, "APcommander", win_w / 2, 36,
                           COL_ACCENT);
        draw_text_centered(r, font_sm, "Local system utilities", win_w / 2,
                           70, COL_DIM);

        draw_tile(r, font_lg, font_sm, &tiles[0]);
        draw_tile(r, font_lg, font_sm, &tiles[1]);

        if (status[0]) {
            draw_text_centered(r, font_sm, status, win_w / 2, win_h - 56,
                               COL_DANGER);
        }
        draw_text_centered(r, font_sm,
                           "Click a tile or press 1 / 2 to launch.   Esc to quit.",
                           win_w / 2, win_h - 28, COL_DIM);

        SDL_RenderPresent(r);
        SDL_Delay(16);
    }

    TTF_CloseFont(font_lg);
    TTF_CloseFont(font_sm);
    TTF_Quit();
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
