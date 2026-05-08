#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <SDL.h>
#include <SDL_ttf.h>

#include <stddef.h>

#include "lsof_parse.h"
#include "ps_query.h"

#define UI_FILTER_MAX 256

typedef struct Ui {
    TTF_Font *font;
    SDL_Window *window;
    SDL_Renderer *renderer;
    int win_w;
    int win_h;

    int table_scroll;
    int selected_visible; /* -1 = none */
    char filter[UI_FILTER_MAX];
    int filter_focus;

    /* Two-step kill: show confirm when == selected row pid. The
     * accompanying starttime is captured from /proc/<pid>/stat at
     * arm-time and re-checked before kill() — together they fingerprint
     * the process across PID recycling, so a long-open confirm dialog
     * can't end up signalling a different process that happens to
     * have grabbed the same PID. */
    int kill_confirm_pid;
    unsigned long long kill_confirm_starttime;
} Ui;

int ui_init(Ui *ui, const char *font_path, int font_px);
void ui_shutdown(Ui *ui);

void ui_draw(Ui *ui, const PortRow *visible, size_t visible_count,
             const ProcessDetail *detail, const char *status_line);

/*
 * Returns: 0 = nothing, 1 = quit, 2 = refresh, 3 = filter changed,
 *          4 = selection changed (reload process detail),
 *          5 = user confirmed SIGTERM kill (main runs kill(2) then refresh)
 */
int ui_handle_event(Ui *ui, const SDL_Event *e, const PortRow *visible,
                    size_t visible_count, int *out_table_top_y);

#endif
