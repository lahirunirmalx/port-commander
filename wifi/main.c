#define _GNU_SOURCE
#include "nmcli_action.h"
#include "nmcli_query.h"
#include "ui_render.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *const FONT_CANDIDATES[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf",
    NULL,
};

static int row_matches(const WifiRow *r, const char *filter)
{
    char hay[1024];

    if (!filter || !filter[0])
        return 1;
    snprintf(hay, sizeof(hay), "%s %s %s %d", r->ssid, r->security, r->bssid,
             r->signal);
    return strcasestr(hay, filter) != NULL;
}

static int build_visible(const WifiTable *t, const char *filter,
                         WifiRow **out, size_t *outn)
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
            WifiRow *nr;
            size_t ncap = cap ? cap * 2 : 128;

            nr = realloc(*out, ncap * sizeof(WifiRow));
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

static const char *band_str(UiBand b)
{
    switch (b) {
    case BAND_BG:
        return "bg";
    case BAND_A:
        return "a";
    default:
        return "";
    }
}

/* nmcli has historically echoed failing argv (including 'password <psk>') in
 * stderr. Replace any "password <token>" run with "password <redacted>" so
 * the PSK is not surfaced into the status line. */
static void scrub_password(char *s)
{
    char *p;
    char *q;

    if (!s)
        return;
    p = strstr(s, "password ");
    while (p) {
        q = p + strlen("password ");
        while (*q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t')
            *q++ = '*';
        p = strstr(q, "password ");
    }
}

static void zero_buffer(volatile char *buf, size_t n)
{
    while (n--)
        *buf++ = 0;
}

static void prefill_form_from_state(Ui *ui, const WifiState *s)
{
    /* When a hotspot is active, mirror its config into the form so the user
     * can see what they have running. Don't overwrite while the user is
     * actively editing (focus on a form field). */
    if (ui->focus == FOCUS_SSID || ui->focus == FOCUS_PASSWORD)
        return;
    if (s->hotspot_active) {
        if (s->hotspot_ssid[0]) {
            strncpy(ui->ssid_input, s->hotspot_ssid,
                    sizeof(ui->ssid_input) - 1);
            ui->ssid_input[sizeof(ui->ssid_input) - 1] = '\0';
        }
        if (s->hotspot_password[0]) {
            strncpy(ui->password_input, s->hotspot_password,
                    sizeof(ui->password_input) - 1);
            ui->password_input[sizeof(ui->password_input) - 1] = '\0';
        }
        if (strcmp(s->hotspot_band, "bg") == 0)
            ui->band = BAND_BG;
        else if (strcmp(s->hotspot_band, "a") == 0)
            ui->band = BAND_A;
        /* else keep whatever the user had */
    } else if (ui->ssid_input[0] == '\0') {
        strncpy(ui->ssid_input, "MyHotspot", sizeof(ui->ssid_input) - 1);
        ui->ssid_input[sizeof(ui->ssid_input) - 1] = '\0';
    }
}

int main(int argc, char *argv[])
{
    Ui ui;
    WifiTable table;
    WifiState state;
    WifiRow *visible = NULL;
    size_t visible_count = 0;
    char status[1024];
    char sticky_msg[512];      /* shown when set; cleared on next user action */
    time_t sticky_until = 0;   /* monotonic-ish: clear after this time_t */
    char action_err[512];
    const char *font_path = NULL;
    int i;
    int running = 1;
    int need_refresh = 1;
    int need_rebuild_filter = 1;
    time_t last_refresh = 0;

    (void)argc;
    (void)argv;

    wifi_table_init(&table);
    wifi_state_init(&state);
    status[0] = '\0';
    sticky_msg[0] = '\0';
    action_err[0] = '\0';

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

    ui.window = SDL_CreateWindow("Wi-Fi Commander", SDL_WINDOWPOS_UNDEFINED,
                                 SDL_WINDOWPOS_UNDEFINED, 1100, 720,
                                 SDL_WINDOW_RESIZABLE);
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
            int r = ui_handle_event(&ui, &e, visible, visible_count, &state);

            if (r == 1) {
                running = 0;
                break;
            }
            if (r == 2)
                need_refresh = 1;
            if (r == 3) {
                need_rebuild_filter = 1;
                ui.selected_visible = -1;
            }
            if (r == 4) {
                /* selection changed — nothing extra to load right now */
            }
            if (r == 5) {
                int rc;

                action_err[0] = '\0';
                rc = nmcli_hotspot_up(state.ifname, ui.ssid_input,
                                      ui.password_input, band_str(ui.band),
                                      action_err, sizeof(action_err));
                scrub_password(action_err);
                if (rc == 0)
                    snprintf(sticky_msg, sizeof(sticky_msg),
                             "Hotspot started: SSID=%s", ui.ssid_input);
                else
                    snprintf(sticky_msg, sizeof(sticky_msg),
                             "Start failed: %s",
                             action_err[0] ? action_err : "unknown error");
                sticky_until = time(NULL) + 8;
                need_refresh = 1;
            }
            if (r == 6) {
                int rc;

                action_err[0] = '\0';
                rc = nmcli_hotspot_down(state.hotspot_conn, action_err,
                                        sizeof(action_err));
                scrub_password(action_err);
                if (rc == 0)
                    snprintf(sticky_msg, sizeof(sticky_msg),
                             "Hotspot stopped.");
                else
                    snprintf(sticky_msg, sizeof(sticky_msg),
                             "Stop failed: %s",
                             action_err[0] ? action_err : "unknown error");
                sticky_until = time(NULL) + 8;
                need_refresh = 1;
            }
        }

        if (!running)
            break;

        if (need_refresh) {
            (void)wifi_state_refresh(&state);
            (void)wifi_table_refresh(&table, state.ifname);
            last_refresh = time(NULL);
            prefill_form_from_state(&ui, &state);
            need_rebuild_filter = 1;
            ui.selected_visible = -1;
            need_refresh = 0;
        }

        if (need_rebuild_filter) {
            if (build_visible(&table, ui.filter, &visible, &visible_count) !=
                0) {
                snprintf(status, sizeof(status),
                         "Out of memory building list.");
            } else if (ui.selected_visible >= (int)visible_count) {
                ui.selected_visible = (int)visible_count - 1;
            }
            need_rebuild_filter = 0;
        }

        if (sticky_msg[0] && time(NULL) >= sticky_until)
            sticky_msg[0] = '\0';

        if (sticky_msg[0]) {
            strncpy(status, sticky_msg, sizeof(status) - 1);
            status[sizeof(status) - 1] = '\0';
        } else {
            struct tm tm_buf;
            struct tm *tm = localtime_r(&last_refresh, &tm_buf);
            char tstr[64];

            if (last_refresh == 0)
                strncpy(tstr, "never", sizeof(tstr));
            else
                strftime(tstr, sizeof(tstr), "%H:%M:%S", tm);

            if (table.err[0])
                snprintf(status, sizeof(status),
                         "%zu networks | last refresh %s | %s", table.count,
                         tstr, table.err);
            else if (state.err[0])
                snprintf(status, sizeof(status),
                         "%zu networks | last refresh %s | %s", table.count,
                         tstr, state.err);
            else
                snprintf(status, sizeof(status),
                         "%zu networks | last refresh %s", table.count, tstr);
        }

        ui_draw(&ui, visible, visible_count, &state, status);
        SDL_RenderPresent(ui.renderer);
        SDL_Delay(16);
    }

    free(visible);
    /* Wipe password material before tearing down the window — best-effort
     * defense against the buffer landing in a core dump. */
    zero_buffer(ui.password_input, sizeof(ui.password_input));
    zero_buffer(state.hotspot_password, sizeof(state.hotspot_password));
    ui_shutdown(&ui);
    wifi_table_free(&table);
    SDL_DestroyRenderer(ui.renderer);
    SDL_DestroyWindow(ui.window);
    SDL_Quit();
    return 0;
}
