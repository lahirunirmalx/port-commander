#include "ui_render.h"
#include "qr_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITLE_H 30
#define FILTER_H 36
#define TABLE_HEADER_H 24
#define ROW_H 22
#define STATUS_H 26
#define PAD 8
#define BTN_H 30
#define INPUT_H 28
#define LABEL_H 18
#define FIELD_GAP 8
#define RIGHT_PANEL_W 380
#define REFRESH_BTN_W 96

static const SDL_Color COL_BG = { 26, 28, 34, 255 };
static const SDL_Color COL_PANEL = { 40, 42, 52, 255 };
static const SDL_Color COL_INPUT = { 30, 32, 40, 255 };
static const SDL_Color COL_ZEBRA_A = { 34, 36, 44, 255 };
static const SDL_Color COL_ZEBRA_B = { 30, 32, 40, 255 };
static const SDL_Color COL_SELECT = { 52, 78, 120, 255 };
static const SDL_Color COL_TEXT = { 230, 232, 238, 255 };
static const SDL_Color COL_DIM = { 150, 154, 168, 255 };
static const SDL_Color COL_ACCENT = { 100, 180, 255, 255 };
static const SDL_Color COL_GOOD = { 120, 200, 130, 255 };
static const SDL_Color COL_DANGER = { 200, 90, 90, 255 };
static const SDL_Color COL_DANGER_BG = { 72, 38, 42, 255 };
static const SDL_Color COL_BTN_BG = { 58, 62, 78, 255 };
static const SDL_Color COL_PRIMARY_BG = { 38, 70, 110, 255 };

typedef struct Layout {
    SDL_Rect filter_input;
    SDL_Rect refresh_btn;
    SDL_Rect table_header;
    SDL_Rect table_body;
    SDL_Rect right_panel;
    SDL_Rect ssid_input;
    SDL_Rect password_input;
    SDL_Rect show_pwd_btn;
    SDL_Rect band_auto;
    SDL_Rect band_bg;
    SDL_Rect band_a;
    SDL_Rect action_btn;
    SDL_Rect qr_box;        /* white background rect; 0×0 when no QR */
    SDL_Rect qr_pixels;     /* drawable QR rect inside qr_box (with padding) */
    int qr_module;          /* px per QR module; 0 when no QR */
    int hotspot_active;
    int form_y0; /* top of the form section, used by draw */
} Layout;

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

static void draw_text_clamp(SDL_Renderer *r, TTF_Font *font, const char *text,
                            int x, int y, int max_w, SDL_Color fg)
{
    char buf[512];
    int w;

    if (!text)
        return;
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    while (buf[0] && TTF_SizeUTF8(font, buf, &w, NULL) == 0 && w > max_w) {
        size_t n = strlen(buf);

        if (n <= 1)
            break;
        buf[n - 1] = '\0';
    }
    draw_text(r, font, buf, x, y, fg);
}

static void draw_button(SDL_Renderer *r, TTF_Font *font, SDL_Rect b,
                        const char *label, SDL_Color bg, SDL_Color border,
                        SDL_Color textc)
{
    int tw = 0;
    int th = 0;

    draw_rect(r, b, bg);
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, 255);
    SDL_RenderDrawRect(r, &b);
    if (TTF_SizeUTF8(font, label, &tw, &th) != 0) {
        th = 14;
        tw = 0;
    }
    draw_text(r, font, label, b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2,
              textc);
}

static void mask_password(const char *src, char *dst, size_t dstsz)
{
    size_t n;
    size_t i;

    if (dstsz == 0)
        return;
    n = strlen(src);
    if (n >= dstsz)
        n = dstsz - 1;
    for (i = 0; i < n; i++)
        dst[i] = '*';
    dst[n] = '\0';
}

static void compute_layout(const Ui *ui, const WifiState *state, Layout *L)
{
    int right_w = RIGHT_PANEL_W;
    int main_y = TITLE_H + FILTER_H;
    int main_h = ui->win_h - main_y - STATUS_H;
    int left_w;
    int rx;
    int y;

    if (right_w > ui->win_w / 2)
        right_w = ui->win_w / 2;
    if (right_w < 240)
        right_w = 240;

    left_w = ui->win_w - right_w;

    L->filter_input.x = PAD + 56;
    L->filter_input.y = TITLE_H + 5;
    L->filter_input.w = left_w - PAD * 2 - 56 - REFRESH_BTN_W - PAD;
    L->filter_input.h = INPUT_H;

    L->refresh_btn.x = left_w - PAD - REFRESH_BTN_W;
    L->refresh_btn.y = TITLE_H + 5;
    L->refresh_btn.w = REFRESH_BTN_W;
    L->refresh_btn.h = INPUT_H;

    L->table_header.x = 0;
    L->table_header.y = main_y;
    L->table_header.w = left_w;
    L->table_header.h = TABLE_HEADER_H;

    L->table_body.x = 0;
    L->table_body.y = main_y + TABLE_HEADER_H;
    L->table_body.w = left_w;
    L->table_body.h = main_h - TABLE_HEADER_H;
    if (L->table_body.h < 1)
        L->table_body.h = 1;

    L->right_panel.x = left_w;
    L->right_panel.y = TITLE_H;
    L->right_panel.w = right_w;
    L->right_panel.h = ui->win_h - TITLE_H - STATUS_H;

    rx = L->right_panel.x + PAD;
    L->hotspot_active = state ? state->hotspot_active : 0;

    /* Form starts after the status block (3 status lines + spacing). */
    y = L->right_panel.y + PAD + LABEL_H + 4 /* heading */
        + (LABEL_H + 2) * 3                  /* 3 status lines */
        + PAD;
    L->form_y0 = y;
    /* The form heading ("Hotspot setup" / "Hotspot (read-only)") lives at
     * form_y0; advance past it before laying out the first field, otherwise
     * the SSID label (drawn at ssid_input.y - LABEL_H - 2) would land on
     * the same baseline as the heading. */
    y += LABEL_H + 6;

    /* SSID label + input */
    y += LABEL_H + 2;
    L->ssid_input.x = rx;
    L->ssid_input.y = y;
    L->ssid_input.w = L->right_panel.w - PAD * 2;
    L->ssid_input.h = INPUT_H;
    y += INPUT_H + FIELD_GAP;

    /* Password label + input + show/hide toggle */
    y += LABEL_H + 2;
    L->show_pwd_btn.w = 64;
    L->show_pwd_btn.h = INPUT_H;
    L->show_pwd_btn.x = L->right_panel.x + L->right_panel.w - PAD - L->show_pwd_btn.w;
    L->show_pwd_btn.y = y;
    L->password_input.x = rx;
    L->password_input.y = y;
    L->password_input.w =
        L->right_panel.w - PAD * 2 - L->show_pwd_btn.w - 6;
    L->password_input.h = INPUT_H;
    y += INPUT_H + FIELD_GAP;

    /* Band buttons */
    y += LABEL_H + 2;
    {
        int total_w = L->right_panel.w - PAD * 2;
        int gap = 6;
        int bw = (total_w - gap * 2) / 3;

        L->band_auto.x = rx;
        L->band_auto.y = y;
        L->band_auto.w = bw;
        L->band_auto.h = BTN_H;

        L->band_bg.x = rx + bw + gap;
        L->band_bg.y = y;
        L->band_bg.w = bw;
        L->band_bg.h = BTN_H;

        L->band_a.x = rx + (bw + gap) * 2;
        L->band_a.y = y;
        L->band_a.w = bw;
        L->band_a.h = BTN_H;
    }
    y += BTN_H + FIELD_GAP * 2;

    /* Action button */
    L->action_btn.x = rx;
    L->action_btn.y = y;
    L->action_btn.w = L->right_panel.w - PAD * 2;
    L->action_btn.h = BTN_H + 4;

    /* QR (only when hotspot is up and qrencode produced output) */
    L->qr_module = 0;
    L->qr_box.w = 0;
    L->qr_box.h = 0;
    if (L->hotspot_active && state && state->qr_text[0]) {
        int qr_cols = 0;
        int qr_text_rows = 0;
        int qr_module_rows;
        int avail_w;
        int avail_h;
        int max_module;
        int module_px;
        int qr_w;
        int qr_h;
        int margin = 12;

        qr_text_dims(state->qr_text, &qr_cols, &qr_text_rows);
        qr_module_rows = qr_text_rows * 2;
        if (qr_cols > 0 && qr_module_rows > 0) {
            /* Layout below the action button:
             *   PAD gap | "Scan…" label | PAD gap | white QR box | … */
            int label_band = PAD + LABEL_H + 4 + PAD;
            int qr_top_y = L->action_btn.y + L->action_btn.h + label_band;

            avail_w = L->right_panel.w - PAD * 2 - margin * 2;
            avail_h = L->right_panel.y + L->right_panel.h - qr_top_y
                      - margin * 2 - PAD;
            max_module = avail_w / qr_cols;
            if (avail_h / qr_module_rows < max_module)
                max_module = avail_h / qr_module_rows;
            if (max_module > 12)
                max_module = 12;
            if (max_module >= 2) {
                module_px = max_module;
                qr_w = qr_cols * module_px;
                qr_h = qr_module_rows * module_px;

                L->qr_module = module_px;
                L->qr_box.x = L->right_panel.x +
                              (L->right_panel.w - qr_w - margin * 2) / 2;
                L->qr_box.y = qr_top_y;
                L->qr_box.w = qr_w + margin * 2;
                L->qr_box.h = qr_h + margin * 2;
                L->qr_pixels.x = L->qr_box.x + margin;
                L->qr_pixels.y = L->qr_box.y + margin;
                L->qr_pixels.w = qr_w;
                L->qr_pixels.h = qr_h;
            }
        }
    }
}

int ui_init(Ui *ui, const char *font_path, int font_px)
{
    ui->font = NULL;
    ui->win_w = 0;
    ui->win_h = 0;
    ui->table_scroll = 0;
    ui->selected_visible = -1;
    ui->filter[0] = '\0';
    ui->ssid_input[0] = '\0';
    ui->password_input[0] = '\0';
    ui->band = BAND_AUTO;
    ui->show_password = 0;
    ui->focus = FOCUS_NONE;
    if (TTF_Init() != 0)
        return -1;
    ui->font = TTF_OpenFont(font_path, font_px);
    if (!ui->font) {
        TTF_Quit();
        return -1;
    }
    return 0;
}

void ui_shutdown(Ui *ui)
{
    if (ui->font) {
        TTF_CloseFont(ui->font);
        ui->font = NULL;
    }
    TTF_Quit();
}

static void draw_input(SDL_Renderer *r, TTF_Font *font, SDL_Rect box,
                       const char *text, int focused, int mask)
{
    char buf[UI_INPUT_MAX + 8];

    draw_rect(r, box, COL_INPUT);
    draw_rect_outline(r, box, focused ? COL_ACCENT : COL_DIM);
    if (mask)
        mask_password(text ? text : "", buf, sizeof(buf));
    else {
        strncpy(buf, text ? text : "", sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    }
    draw_text_clamp(r, font, buf, box.x + 6, box.y + (box.h - 14) / 2,
                    box.w - 12, COL_TEXT);
}

static void draw_band_btn(SDL_Renderer *r, TTF_Font *font, SDL_Rect b,
                          const char *label, int selected)
{
    SDL_Color bg = selected ? COL_PRIMARY_BG : COL_BTN_BG;
    SDL_Color border = selected ? COL_ACCENT : COL_DIM;

    draw_button(r, font, b, label, bg, border, COL_TEXT);
}

void ui_draw(Ui *ui, const WifiRow *visible, size_t visible_count,
             const WifiState *state, const char *status_line)
{
    SDL_Renderer *r = ui->renderer;
    Layout L;
    int i;
    int max_rows;
    int row0;
    size_t idx;
    int col_use;
    int col_sig;
    int col_sec;
    int col_ssid;
    int col_bssid;

    SDL_GetWindowSize(ui->window, &ui->win_w, &ui->win_h);
    compute_layout(ui, state, &L);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    draw_rect(r, (SDL_Rect){ 0, 0, ui->win_w, ui->win_h }, COL_BG);

    /* Title bar */
    draw_rect(r, (SDL_Rect){ 0, 0, ui->win_w, TITLE_H }, COL_PANEL);
    draw_text(r, ui->font, "Wi-Fi Commander", PAD, 7, COL_ACCENT);
    draw_text(r, ui->font,
              "F5 refresh   Ctrl+F filter   Esc blur/quit", PAD + 200, 7,
              COL_DIM);

    /* Filter row */
    draw_text(r, ui->font, "Filter:", PAD, TITLE_H + 9, COL_DIM);
    draw_input(r, ui->font, L.filter_input, ui->filter,
               ui->focus == FOCUS_FILTER, 0);
    draw_button(r, ui->font, L.refresh_btn, "Refresh (F5)", COL_BTN_BG,
                COL_ACCENT, COL_TEXT);

    /* Table header */
    col_use = PAD;
    col_sig = PAD + 36;
    col_sec = PAD + 86;
    col_ssid = PAD + 170;
    col_bssid = L.table_header.w - 180;
    if (col_bssid < col_ssid + 100)
        col_bssid = col_ssid + 100;

    draw_rect(r, L.table_header, COL_PANEL);
    draw_text(r, ui->font, "*", col_use, L.table_header.y + 4, COL_ACCENT);
    draw_text(r, ui->font, "Sig", col_sig, L.table_header.y + 4, COL_ACCENT);
    draw_text(r, ui->font, "Security", col_sec, L.table_header.y + 4,
              COL_ACCENT);
    draw_text(r, ui->font, "SSID", col_ssid, L.table_header.y + 4, COL_ACCENT);
    draw_text(r, ui->font, "BSSID", col_bssid, L.table_header.y + 4,
              COL_ACCENT);

    /* Table body — scroll clamping */
    max_rows = L.table_body.h / ROW_H;
    if (max_rows < 1)
        max_rows = 1;
    if (ui->table_scroll < 0)
        ui->table_scroll = 0;
    {
        int total_h = (int)visible_count * ROW_H;
        int max_scroll = total_h - L.table_body.h;

        if (max_scroll < 0)
            max_scroll = 0;
        if (ui->table_scroll > max_scroll)
            ui->table_scroll = max_scroll;
    }
    row0 = ui->table_scroll / ROW_H;
    if (row0 < 0)
        row0 = 0;

    SDL_RenderSetClipRect(r, &L.table_body);
    for (i = 0; i < max_rows; i++) {
        idx = (size_t)row0 + (size_t)i;
        if (idx >= visible_count)
            break;
        {
            int ry = L.table_body.y + i * ROW_H - (ui->table_scroll % ROW_H);
            SDL_Color rowbg = (idx % 2) ? COL_ZEBRA_B : COL_ZEBRA_A;
            char sigbuf[16];

            if ((int)idx == ui->selected_visible)
                rowbg = COL_SELECT;
            draw_rect(r, (SDL_Rect){ 0, ry, L.table_body.w, ROW_H }, rowbg);

            if (visible[idx].in_use)
                draw_text(r, ui->font, "*", col_use, ry + 3, COL_GOOD);
            snprintf(sigbuf, sizeof(sigbuf), "%d", visible[idx].signal);
            draw_text(r, ui->font, sigbuf, col_sig, ry + 3, COL_TEXT);
            draw_text_clamp(r, ui->font, visible[idx].security, col_sec,
                            ry + 3, col_ssid - col_sec - 4, COL_TEXT);
            draw_text_clamp(r, ui->font, visible[idx].ssid, col_ssid, ry + 3,
                            col_bssid - col_ssid - 8, COL_TEXT);
            draw_text_clamp(r, ui->font, visible[idx].bssid, col_bssid,
                            ry + 3, L.table_body.w - col_bssid - PAD,
                            COL_DIM);
        }
    }
    SDL_RenderSetClipRect(r, NULL);

    /* Right panel: status + form */
    draw_rect(r, L.right_panel, COL_PANEL);
    draw_rect_outline(r, L.right_panel, COL_DIM);
    {
        int rx = L.right_panel.x + PAD;
        int y = L.right_panel.y + PAD;

        draw_text(r, ui->font, "Network", rx, y, COL_ACCENT);
        y += LABEL_H + 4;

        {
            char line[256];

            snprintf(line, sizeof(line), "Interface:  %s",
                     state && state->ifname[0] ? state->ifname : "(none)");
            draw_text_clamp(r, ui->font, line, rx, y,
                            L.right_panel.w - PAD * 2, COL_TEXT);
            y += LABEL_H + 2;

            if (state && state->active_ssid[0])
                snprintf(line, sizeof(line), "Connected:  %s",
                         state->active_ssid);
            else if (state && state->hotspot_active)
                snprintf(line, sizeof(line), "Connected:  (hosting hotspot)");
            else
                snprintf(line, sizeof(line), "Connected:  (none)");
            draw_text_clamp(r, ui->font, line, rx, y,
                            L.right_panel.w - PAD * 2,
                            (state && state->active_ssid[0]) ? COL_TEXT
                                                             : COL_DIM);
            y += LABEL_H + 2;

            if (state && state->hotspot_active) {
                snprintf(line, sizeof(line), "Hotspot:    ACTIVE  (%s)",
                         state->hotspot_ssid[0] ? state->hotspot_ssid
                                                : state->hotspot_conn);
                draw_text_clamp(r, ui->font, line, rx, y,
                                L.right_panel.w - PAD * 2, COL_GOOD);
            } else {
                draw_text(r, ui->font, "Hotspot:    INACTIVE", rx, y,
                          COL_DIM);
            }
        }

        /* Form area */
        y = L.form_y0;
        draw_text(r, ui->font, L.hotspot_active ? "Hotspot (read-only)"
                                                : "Hotspot setup",
                  rx, y, COL_ACCENT);

        /* SSID */
        y = L.ssid_input.y - LABEL_H - 2;
        draw_text(r, ui->font, "SSID", rx, y, COL_DIM);
        draw_input(r, ui->font, L.ssid_input, ui->ssid_input,
                   ui->focus == FOCUS_SSID, 0);

        /* Password */
        y = L.password_input.y - LABEL_H - 2;
        draw_text(r, ui->font, "Password (>= 8 chars)", rx, y, COL_DIM);
        draw_input(r, ui->font, L.password_input, ui->password_input,
                   ui->focus == FOCUS_PASSWORD, !ui->show_password);
        draw_button(r, ui->font, L.show_pwd_btn,
                    ui->show_password ? "Hide" : "Show", COL_BTN_BG, COL_DIM,
                    COL_TEXT);

        /* Band */
        y = L.band_auto.y - LABEL_H - 2;
        draw_text(r, ui->font, "Band", rx, y, COL_DIM);
        draw_band_btn(r, ui->font, L.band_auto, "Auto",
                      ui->band == BAND_AUTO);
        draw_band_btn(r, ui->font, L.band_bg, "2.4 GHz",
                      ui->band == BAND_BG);
        draw_band_btn(r, ui->font, L.band_a, "5 GHz", ui->band == BAND_A);

        /* Action button */
        if (L.hotspot_active) {
            draw_button(r, ui->font, L.action_btn, "Stop Hotspot",
                        COL_DANGER_BG, COL_DANGER, COL_TEXT);
        } else {
            draw_button(r, ui->font, L.action_btn, "Start Hotspot",
                        COL_PRIMARY_BG, COL_ACCENT, COL_TEXT);
        }

        /* QR code for the active hotspot */
        if (L.qr_module > 0) {
            const SDL_Color white = { 255, 255, 255, 255 };
            int label_y = L.action_btn.y + L.action_btn.h + PAD;

            draw_text(r, ui->font, "Scan with phone to join:", rx, label_y,
                      COL_DIM);
            draw_rect(r, L.qr_box, white);
            qr_render(r, L.qr_pixels.x, L.qr_pixels.y, L.qr_module,
                      state->qr_text);
        } else if (L.hotspot_active && state && !state->qr_text[0]) {
            int hint_y = L.action_btn.y + L.action_btn.h + PAD * 2;

            draw_text_clamp(r, ui->font,
                            "QR unavailable (install 'qrencode' to enable)",
                            rx, hint_y, L.right_panel.w - PAD * 2, COL_DIM);
        }
    }

    /* Status line */
    {
        SDL_Rect sb = { 0, ui->win_h - STATUS_H, ui->win_w, STATUS_H };

        draw_rect(r, sb, COL_PANEL);
        if (status_line && status_line[0]) {
            SDL_Color c = (strstr(status_line, "error") ||
                           strstr(status_line, "Error") ||
                           strstr(status_line, "fail") ||
                           strstr(status_line, "Fail"))
                              ? COL_DANGER
                              : COL_DIM;

            draw_text_clamp(r, ui->font, status_line, PAD, sb.y + 5,
                            ui->win_w - PAD * 2, c);
        }
    }
}

static void set_focus(Ui *ui, UiFocus f)
{
    if (ui->focus == f)
        return;
    ui->focus = f;
    if (f == FOCUS_NONE)
        SDL_StopTextInput();
    else
        SDL_StartTextInput();
}

static char *focused_buffer(Ui *ui, size_t *cap_out)
{
    switch (ui->focus) {
    case FOCUS_FILTER:
        *cap_out = sizeof(ui->filter);
        return ui->filter;
    case FOCUS_SSID:
        *cap_out = sizeof(ui->ssid_input);
        return ui->ssid_input;
    case FOCUS_PASSWORD:
        *cap_out = sizeof(ui->password_input);
        return ui->password_input;
    default:
        *cap_out = 0;
        return NULL;
    }
}

int ui_handle_event(Ui *ui, const SDL_Event *e, const WifiRow *visible,
                    size_t visible_count, const WifiState *state)
{
    Layout L;
    int filter_changed_signal = (ui->focus == FOCUS_FILTER) ? 3 : 0;

    (void)visible; /* only visible_count is consulted here */

    if (ui->window)
        SDL_GetWindowSize(ui->window, &ui->win_w, &ui->win_h);
    compute_layout(ui, state, &L);

    switch (e->type) {
    case SDL_QUIT:
        return 1;

    case SDL_KEYDOWN:
        if (ui->focus != FOCUS_NONE) {
            if (e->key.keysym.sym == SDLK_ESCAPE ||
                e->key.keysym.sym == SDLK_RETURN ||
                e->key.keysym.sym == SDLK_KP_ENTER) {
                set_focus(ui, FOCUS_NONE);
                return 0;
            }
            if (e->key.keysym.sym == SDLK_BACKSPACE) {
                size_t cap;
                char *buf = focused_buffer(ui, &cap);

                if (buf) {
                    size_t n = strlen(buf);

                    if (n > 0) {
                        buf[n - 1] = '\0';
                        return filter_changed_signal;
                    }
                }
                return 0;
            }
            if (e->key.keysym.sym == SDLK_v &&
                (e->key.keysym.mod & KMOD_CTRL)) {
                char *clip = SDL_GetClipboardText();

                if (clip) {
                    size_t cap;
                    char *buf = focused_buffer(ui, &cap);

                    if (buf && cap > 0) {
                        size_t n = strlen(buf);
                        size_t add = strlen(clip);
                        size_t i;

                        for (i = 0; i < add && n + 1 < cap; i++) {
                            if (clip[i] == '\n' || clip[i] == '\r')
                                continue;
                            buf[n++] = clip[i];
                        }
                        buf[n] = '\0';
                    }
                    SDL_free(clip);
                    return filter_changed_signal;
                }
                return 0;
            }
            return 0;
        }
        if (e->key.keysym.sym == SDLK_F5)
            return 2;
        if (e->key.keysym.sym == SDLK_ESCAPE)
            return 1;
        if (e->key.keysym.sym == SDLK_f &&
            (e->key.keysym.mod & KMOD_CTRL)) {
            set_focus(ui, FOCUS_FILTER);
            return 0;
        }
        return 0;

    case SDL_TEXTINPUT:
        if (ui->focus != FOCUS_NONE) {
            size_t cap;
            char *buf = focused_buffer(ui, &cap);

            if (buf && cap > 0) {
                size_t n = strlen(buf);
                size_t add = strlen(e->text.text);

                if (n + add < cap) {
                    memcpy(buf + n, e->text.text, add + 1);
                    return filter_changed_signal;
                }
            }
        }
        return 0;

    case SDL_MOUSEWHEEL:
        if (ui->focus == FOCUS_NONE) {
            int delta = e->wheel.y * ROW_H * 3;

            ui->table_scroll -= delta;
        }
        return 0;

    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button != SDL_BUTTON_LEFT)
            return 0;
        {
            int mx = e->button.x;
            int my = e->button.y;

            if (pt_in_rect(mx, my, L.filter_input)) {
                set_focus(ui, FOCUS_FILTER);
                return 0;
            }
            if (pt_in_rect(mx, my, L.refresh_btn))
                return 2;

            if (pt_in_rect(mx, my, L.ssid_input)) {
                if (!L.hotspot_active)
                    set_focus(ui, FOCUS_SSID);
                return 0;
            }
            if (pt_in_rect(mx, my, L.password_input)) {
                if (!L.hotspot_active)
                    set_focus(ui, FOCUS_PASSWORD);
                return 0;
            }
            if (pt_in_rect(mx, my, L.show_pwd_btn)) {
                ui->show_password = !ui->show_password;
                return 0;
            }
            if (!L.hotspot_active) {
                if (pt_in_rect(mx, my, L.band_auto)) {
                    ui->band = BAND_AUTO;
                    return 0;
                }
                if (pt_in_rect(mx, my, L.band_bg)) {
                    ui->band = BAND_BG;
                    return 0;
                }
                if (pt_in_rect(mx, my, L.band_a)) {
                    ui->band = BAND_A;
                    return 0;
                }
            }
            if (pt_in_rect(mx, my, L.action_btn)) {
                set_focus(ui, FOCUS_NONE);
                return L.hotspot_active ? 6 : 5;
            }

            /* Click anywhere else clears focus first. */
            set_focus(ui, FOCUS_NONE);

            /* Table row click */
            if (pt_in_rect(mx, my, L.table_body)) {
                int rel = my - L.table_body.y + ui->table_scroll;
                int row = rel / ROW_H;

                if (row >= 0 && (size_t)row < visible_count) {
                    ui->selected_visible = row;
                    return 4;
                }
                ui->selected_visible = -1;
                return 4;
            }
        }
        return 0;

    default:
        return 0;
    }
}
