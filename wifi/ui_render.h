#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <SDL.h>
#include <SDL_ttf.h>

#include <stddef.h>

#include "nmcli_query.h"

#define UI_FILTER_MAX 128
/* Fits a 63-char WPA passphrase (max for ASCII PSK) and a 32-byte SSID with
 * room to spare. Don't reduce below 64 + NUL. */
#define UI_INPUT_MAX 128

typedef enum UiFocus {
    FOCUS_NONE = 0,
    FOCUS_FILTER,
    FOCUS_SSID,
    FOCUS_PASSWORD,
} UiFocus;

typedef enum UiBand {
    BAND_AUTO = 0,
    BAND_BG = 1, /* 2.4 GHz */
    BAND_A = 2,  /* 5 GHz */
} UiBand;

typedef struct Ui {
    TTF_Font *font;
    SDL_Window *window;
    SDL_Renderer *renderer;
    int win_w;
    int win_h;

    int table_scroll;
    int selected_visible; /* -1 = none */
    char filter[UI_FILTER_MAX];

    char ssid_input[UI_INPUT_MAX];
    char password_input[UI_INPUT_MAX];
    UiBand band;
    int show_password;

    UiFocus focus;
} Ui;

int ui_init(Ui *ui, const char *font_path, int font_px);
void ui_shutdown(Ui *ui);

void ui_draw(Ui *ui, const WifiRow *visible, size_t visible_count,
             const WifiState *state, const char *status_line);

/*
 * Returns: 0 = nothing, 1 = quit, 2 = refresh, 3 = filter changed,
 *          4 = selection changed, 5 = start hotspot (use ui->ssid_input,
 *          ui->password_input, ui->band), 6 = stop hotspot.
 */
int ui_handle_event(Ui *ui, const SDL_Event *e, const WifiRow *visible,
                    size_t visible_count, const WifiState *state);

#endif
