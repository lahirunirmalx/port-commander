/*
 * APcommander dashboard — top-level orchestrator.
 *
 * Wires the four dashboard layers together:
 *   - config     load tiles from an INI file (search chain in config.c)
 *   - menu       curved-menu state, layout, and rendering
 *   - spawn_pool non-blocking child-process tracker
 *   - render     shared SDL2 drawing primitives + colour palette
 *
 * Intentionally thin: SDL setup, font loading, the main loop, and the
 * window chrome (header / footer / status). All other concerns belong
 * in the layers above.
 */
#include "compat.h"
#include "config.h"
#include "menu.h"
#include "render.h"
#include "spawn_pool.h"
#include "tile.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <stdio.h>
#include <string.h>

/* Capacity limits. Set generously — the window scrolls so a tall list
 * still works, and parallel child count is bounded only by the user's
 * desktop, not our tracking table. */
#define MAX_TILES    64
#define MAX_CHILDREN 64

#define WIN_DEFAULT_W 720
#define WIN_DEFAULT_H 660

#define HEADER_TITLE_Y    24
#define HEADER_SUBTITLE_Y 56
#define FOOTER_STATUS_Y   44   /* px from window bottom (status_msg / pool) */
#define FOOTER_HINT_Y     22   /* px from window bottom (key hint)          */

#define FONT_LG_PT 22
#define FONT_MD_PT 16
#define FONT_SM_PT 12

/* ------------------------------------------------------------------------- */

static int sdl_init(SDL_Window **win, SDL_Renderer **r)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    *win = SDL_CreateWindow("APcommander", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED,
                            WIN_DEFAULT_W, WIN_DEFAULT_H,
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

static void sdl_quit(SDL_Window *win, SDL_Renderer *r)
{
    TTF_Quit();
    if (r) SDL_DestroyRenderer(r);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

static int open_fonts(const char *path, TTF_Font **lg, TTF_Font **md,
                      TTF_Font **sm)
{
    *lg = TTF_OpenFont(path, FONT_LG_PT);
    *md = TTF_OpenFont(path, FONT_MD_PT);
    *sm = TTF_OpenFont(path, FONT_SM_PT);
    if (!*lg || !*md || !*sm) {
        fprintf(stderr, "TTF_OpenFont: %s\n", TTF_GetError());
        if (*lg) TTF_CloseFont(*lg);
        if (*md) TTF_CloseFont(*md);
        if (*sm) TTF_CloseFont(*sm);
        *lg = *md = *sm = NULL;
        return -1;
    }
    return 0;
}

static void close_fonts(TTF_Font *lg, TTF_Font *md, TTF_Font *sm)
{
    if (lg) TTF_CloseFont(lg);
    if (md) TTF_CloseFont(md);
    if (sm) TTF_CloseFont(sm);
}

/* ------------------------------------------------------------------------- */

static void render_chrome(SDL_Renderer *r, TTF_Font *lg, TTF_Font *sm,
                          int win_w, int win_h, const char *status,
                          const char *config_path, int running_count)
{
    render_text_centered(r, lg, "APcommander", win_w / 2,
                         HEADER_TITLE_Y, COL_ACCENT);
    render_text_centered(r, sm, "Local system utilities", win_w / 2,
                         HEADER_SUBTITLE_Y, COL_DIM);

    if (status && status[0]) {
        render_text_centered(r, sm, status, win_w / 2,
                             win_h - FOOTER_STATUS_Y, COL_DANGER);
    } else if (running_count > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d tool%s running",
                 running_count, running_count == 1 ? "" : "s");
        render_text_centered(r, sm, buf, win_w / 2,
                             win_h - FOOTER_STATUS_Y, COL_GOOD);
    } else if (config_path && config_path[0]) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "config: %s", config_path);
        render_text_centered(r, sm, buf, win_w / 2,
                             win_h - FOOTER_STATUS_Y, COL_DIM);
    }
    render_text_centered(r, sm,
        "↑/↓ navigate  ·  Enter launch  ·  hotkeys  ·  Esc quit",
        win_w / 2, win_h - FOOTER_HINT_Y, COL_DIM);
}

static void print_no_config_error(const char *app_dir)
{
    fprintf(stderr,
        "No launcher config found. Tried (in order):\n"
        "  argv[1], $APCOMMANDER_CONFIG,\n"
        "  $XDG_CONFIG_HOME/apcommander/tiles.conf "
        "(or ~/.config/apcommander/tiles.conf),\n"
        "  %s/apcommander.conf,\n"
        "  %s/apcommander.default.conf\n"
        "Place a config at one of those paths, or run "
        "`cmake --build build` to install the shipped default next "
        "to the binary.\n", app_dir, app_dir);
}

/* ------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    SDL_Window *win = NULL;
    SDL_Renderer *r = NULL;
    TTF_Font *font_lg = NULL;
    TTF_Font *font_md = NULL;
    TTF_Font *font_sm = NULL;
    SpawnPool *pool = NULL;
    Tile tiles[MAX_TILES];
    Menu menu;
    char status[512];
    char config_path[1024];
    char app_dir[1024];
    char font_path[1024];
    int num_tiles;
    int running = 1;
    int rc = 0;
    int i;

    memset(tiles, 0, sizeof(tiles));
    status[0] = '\0';
    config_path[0] = '\0';

    if (compat_app_dir(app_dir, sizeof(app_dir)) != 0)
        snprintf(app_dir, sizeof(app_dir), ".");

    /* 1. Load tiles from config — must succeed before we open a window. */
    num_tiles = config_load((argc > 1) ? argv[1] : NULL, tiles, MAX_TILES,
                            config_path, sizeof(config_path));
    if (num_tiles < 0) {
        print_no_config_error(app_dir);
        return 1;
    }
    if (num_tiles == 0) {
        fprintf(stderr, "Config %s had no usable [section] entries. "
                        "Each tile needs at least 'title' and 'exec' set.\n",
                        config_path);
        return 1;
    }

    /* 2. Bring up SDL and the font set. */
    if (sdl_init(&win, &r) != 0) {
        rc = 1;
        goto cleanup_tiles;
    }
    if (compat_default_font(font_path, sizeof(font_path)) != 0) {
        fprintf(stderr, "No monospace font found. Install fonts-dejavu-core "
                        "(Debian/Ubuntu) or equivalent.\n");
        rc = 1;
        goto cleanup_sdl;
    }
    if (open_fonts(font_path, &font_lg, &font_md, &font_sm) != 0) {
        rc = 1;
        goto cleanup_sdl;
    }

    /* 3. Spawn pool + menu state. */
    pool = pool_create(MAX_CHILDREN);
    if (!pool) {
        fprintf(stderr, "Out of memory creating spawn pool.\n");
        rc = 1;
        goto cleanup_fonts;
    }
    menu_init(&menu);

    /* 4. Main loop. */
    while (running) {
        SDL_Event e;
        int win_w = 0;
        int win_h = 0;

        pool_reap(pool, tiles, num_tiles);

        while (SDL_PollEvent(&e)) {
            int idx = -1;
            MenuAction action;

            if (e.type == SDL_QUIT) {
                running = 0;
                break;
            }
            action = menu_handle_event(&menu, &e, tiles, num_tiles, &idx);
            if (action == MENU_QUIT) {
                running = 0;
                break;
            }
            if (action == MENU_LAUNCH && idx >= 0 && idx < num_tiles)
                (void)pool_launch(pool, tiles, idx, status, sizeof(status));
        }

        menu_step(&menu);

        SDL_GetWindowSize(win, &win_w, &win_h);
        menu_layout(&menu, tiles, num_tiles, win_w, win_h);

        SDL_SetRenderDrawColor(r, COL_BG.r, COL_BG.g, COL_BG.b, 255);
        SDL_RenderClear(r);

        render_chrome(r, font_lg, font_sm, win_w, win_h, status,
                      config_path, pool_total(pool));
        menu_render(&menu, r, font_md, font_sm, tiles, num_tiles,
                    win_w, win_h);

        SDL_RenderPresent(r);
        SDL_Delay(16);
    }

    pool_destroy_detach(pool);
cleanup_fonts:
    close_fonts(font_lg, font_md, font_sm);
cleanup_sdl:
    sdl_quit(win, r);
cleanup_tiles:
    for (i = 0; i < num_tiles; i++)
        tile_free(&tiles[i]);
    return rc;
}
