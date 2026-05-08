#define _GNU_SOURCE
#include "lsof_parse.h"
#include "ps_query.h"
#include "ui_render.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *const FONT_CANDIDATES[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf",
    NULL,
};

static int row_matches(const PortRow *r, const char *filter)
{
    char hay[2048];

    if (!filter || !filter[0])
        return 1;
    snprintf(hay, sizeof(hay), "%d %s %s %s %s", r->pid, r->comm, r->proto,
             r->name, r->state);
    return strcasestr(hay, filter) != NULL;
}

static int build_visible(const PortTable *t, const char *filter, PortRow **out,
                         size_t *outn)
{
    size_t cap = 0;
    size_t i;

    *outn = 0;
    free(*out);
    *out = NULL;

    for (i = 0; i < t->count; i++) {
        if (!row_matches(&t->rows[i], filter))
            continue;
        if (*outn >= cap) {
            PortRow *nr;
            size_t ncap = cap ? cap * 2 : 512;

            nr = realloc(*out, ncap * sizeof(PortRow));
            if (!nr) {
                free(*out);
                *out = NULL;
                *outn = 0;
                return -1;
            }
            *out = nr;
            cap = ncap;
        }
        (*out)[*outn] = t->rows[i];
        (*outn)++;
    }
    return 0;
}

static void refresh_detail(ProcessDetail *detail, const PortRow *visible,
                           size_t visible_count, int selected_visible)
{
    memset(detail, 0, sizeof(*detail));

    if (selected_visible < 0 ||
        (size_t)selected_visible >= visible_count) {
        return;
    }
    (void)process_detail_load(visible[selected_visible].pid, detail);
}

int main(int argc, char *argv[])
{
    Ui ui;
    PortTable table;
    PortRow *visible = NULL;
    size_t visible_count = 0;
    ProcessDetail detail;
    char status[1024];
    const char *font_path = NULL;
    int i;
    int running = 1;
    int need_refresh = 1;
    int need_rebuild_filter = 1;
    time_t last_refresh = 0;

    (void)argc;
    (void)argv;

    port_table_init(&table);
    memset(&detail, 0, sizeof(detail));
    status[0] = '\0';

    for (i = 0; FONT_CANDIDATES[i]; i++) {
        FILE *fp = fopen(FONT_CANDIDATES[i], "r");
        if (fp) {
            fclose(fp);
            font_path = FONT_CANDIDATES[i];
            break;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    ui.window = SDL_CreateWindow(
        "Port Commander", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        1100, 780, SDL_WINDOW_RESIZABLE);
    if (!ui.window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    ui.renderer = SDL_CreateRenderer(ui.window, -1, SDL_RENDERER_ACCELERATED);
    if (!ui.renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(ui.window);
        SDL_Quit();
        return 1;
    }

    if (!font_path) {
        fprintf(stderr, "No font found. Install fonts-dejavu-core.\n");
        SDL_DestroyRenderer(ui.renderer);
        SDL_DestroyWindow(ui.window);
        SDL_Quit();
        return 1;
    }

    if (ui_init(&ui, font_path, 14) != 0) {
        fprintf(stderr, "ui_init / TTF_OpenFont: %s\n", TTF_GetError());
        SDL_DestroyRenderer(ui.renderer);
        SDL_DestroyWindow(ui.window);
        SDL_Quit();
        return 1;
    }

    while (running) {
        SDL_Event e;

        while (SDL_PollEvent(&e)) {
            int r = ui_handle_event(&ui, &e, visible, visible_count, NULL);

            if (r == 1) {
                running = 0;
                break;
            }
            if (r == 2)
                need_refresh = 1;
            if (r == 3) {
                need_rebuild_filter = 1;
                ui.selected_visible = -1;
                ui.kill_confirm_pid = -1;
            }
            if (r == 4) {
                ui.kill_confirm_pid = -1;
                refresh_detail(&detail, visible, visible_count,
                               ui.selected_visible);
            }
            if (r == 5) {
                int pid = -1;

                if (ui.selected_visible >= 0 &&
                    (size_t)ui.selected_visible < visible_count)
                    pid = visible[ui.selected_visible].pid;
                if (pid > 0 && pid == ui.kill_confirm_pid) {
                    /* Re-read the process's starttime and refuse the
                     * kill if it no longer matches the value we
                     * captured when the user armed the confirm. The
                     * kernel reuses PID integers; without this check,
                     * a long-open dialog could end up sending SIGTERM
                     * to a different process that happened to inherit
                     * the same PID — particularly bad under sudo. */
                    unsigned long long st_now = proc_read_starttime(pid);

                    if (ui.kill_confirm_starttime != 0 &&
                        st_now == ui.kill_confirm_starttime) {
                        if (kill((pid_t)pid, SIGTERM) != 0)
                            fprintf(stderr, "kill(%d, SIGTERM): %s\n", pid,
                                    strerror(errno));
                    } else {
                        fprintf(stderr,
                                "kill: PID %d no longer refers to the "
                                "process that was armed; refusing.\n", pid);
                    }
                }
                ui.kill_confirm_pid = -1;
                need_refresh = 1;
            }
        }

        if (!running)
            break;

        if (need_refresh) {
            port_table_refresh(&table);
            last_refresh = time(NULL);
            need_rebuild_filter = 1;
            ui.selected_visible = -1;
            ui.kill_confirm_pid = -1;
            memset(&detail, 0, sizeof(detail));
            need_refresh = 0;
        }

        if (need_rebuild_filter) {
            if (build_visible(&table, ui.filter, &visible, &visible_count) !=
                0) {
                snprintf(status, sizeof(status), "Out of memory building list.");
            } else {
                if (ui.selected_visible >= (int)visible_count)
                    ui.selected_visible = (int)visible_count - 1;
                refresh_detail(&detail, visible, visible_count,
                               ui.selected_visible);
            }
            need_rebuild_filter = 0;
        }

        {
            struct tm tm_buf;
            struct tm *tm = localtime_r(&last_refresh, &tm_buf);
            char tstr[64];

            if (last_refresh == 0)
                strncpy(tstr, "never", sizeof(tstr));
            else
                strftime(tstr, sizeof(tstr), "%H:%M:%S", tm);

            if (table.err[0])
                snprintf(status, sizeof(status),
                         "Rows: %zu | Last refresh %s | %s", table.count, tstr,
                         table.err);
            else
                snprintf(status, sizeof(status), "Rows: %zu | Last refresh %s",
                         table.count, tstr);
        }

        ui_draw(&ui, visible, visible_count, &detail, status);
        SDL_RenderPresent(ui.renderer);
        SDL_Delay(16);
    }

    free(visible);
    ui_shutdown(&ui);
    port_table_free(&table);
    SDL_DestroyRenderer(ui.renderer);
    SDL_DestroyWindow(ui.window);
    SDL_Quit();
    return 0;
}
