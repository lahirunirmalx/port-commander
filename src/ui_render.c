#include "ui_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILTER_H 36
#define TABLE_HEADER_H 26
#define DETAIL_H 268
#define ROW_H 20
#define PAD 8
#define FILTER_BOX_H 26
#define CARD_GAP 8
#define BTN_H 28
#define MINI_CARD_H 56
#define HEADER_ROW 34
#define NAME_CARD_H 40
#define CMD_LINE_H 16

static const SDL_Color COL_BG = {26, 28, 34, 255};
static const SDL_Color COL_PANEL = {40, 42, 52, 255};
static const SDL_Color COL_CARD = {48, 50, 62, 255};
static const SDL_Color COL_ZEBRA_A = {34, 36, 44, 255};
static const SDL_Color COL_ZEBRA_B = {30, 32, 40, 255};
static const SDL_Color COL_SELECT = {52, 78, 120, 255};
static const SDL_Color COL_TEXT = {230, 232, 238, 255};
static const SDL_Color COL_DIM = {150, 154, 168, 255};
static const SDL_Color COL_ACCENT = {100, 180, 255, 255};
static const SDL_Color COL_DANGER = {200, 90, 90, 255};
static const SDL_Color COL_DANGER_BG = {72, 38, 42, 255};
static const SDL_Color COL_BTN_BG = {58, 62, 78, 255};

static int table_top_y(void)
{
    return FILTER_H;
}

static int detail_pane_y(const Ui *ui)
{
    int y = ui->win_h - DETAIL_H;
    return y > 0 ? y : 0;
}

static int table_area_h(const Ui *ui)
{
    int y0 = table_top_y() + TABLE_HEADER_H;
    int h = ui->win_h - y0 - DETAIL_H;
    /* Do not inflate past the window (old code used ROW_H*3 min and underflowed). */
    if (h < 1)
        h = 1;
    return h;
}

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

static void draw_text_wrapped(SDL_Renderer *r, TTF_Font *font, const char *text,
                              int x, int y, int max_w, int y_max, int line_h,
                              SDL_Color fg)
{
    const char *end;
    const char *line_start;
    int cy = y;

    if (!text || !text[0])
        return;

    end = text + strlen(text);
    line_start = text;

    while (line_start < end && cy + line_h <= y_max) {
        size_t best = 0;
        size_t try;
        char tmp[640];

        for (try = 1; try <= (size_t)(end - line_start) && try < sizeof(tmp) - 1;
             try++) {
            int tw;

            memcpy(tmp, line_start, try);
            tmp[try] = '\0';
            if (TTF_SizeUTF8(font, tmp, &tw, NULL) != 0)
                break;
            if (tw <= max_w)
                best = try;
            else
                break;
        }
        if (best == 0) {
            best = 1;
            memcpy(tmp, line_start, best);
            tmp[best] = '\0';
        } else {
            memcpy(tmp, line_start, best);
            tmp[best] = '\0';
        }
        draw_text(r, font, tmp, x, cy, fg);
        cy += line_h;
        line_start += best;
        while (line_start < end && *line_start == ' ')
            line_start++;
    }
}

static void draw_card(SDL_Renderer *r, TTF_Font *font, int x, int y, int w,
                      int h, const char *title, const char *value, SDL_Color fg,
                      SDL_Color vfg)
{
    SDL_Rect box = {x, y, w, h};

    draw_rect(r, box, COL_CARD);
    draw_rect_outline(r, box, COL_DIM);
    draw_text(r, font, title, x + 6, y + 4, fg);
    draw_text_clamp(r, font, value ? value : "—", x + 6, y + 4 + ROW_H, w - 12,
                    vfg);
}

static void draw_button(SDL_Renderer *r, TTF_Font *font, SDL_Rect b,
                        const char *label, SDL_Color bg, SDL_Color border,
                        SDL_Color textc)
{
    int tw, th;

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

static void detail_kill_button_layout(const Ui *ui, int sel_pid, SDL_Rect *kill,
                                      SDL_Rect *yes, SDL_Rect *no)
{
    int dy = detail_pane_y(ui);
    int btn_kill_w = 128;
    int btn_yes_w = 168;
    int btn_no_w = 88;
    int x = ui->win_w - PAD - btn_kill_w;

    kill->x = x;
    kill->y = dy + 4;
    kill->w = btn_kill_w;
    kill->h = BTN_H;

    yes->w = 0;
    no->w = 0;
    if (ui->kill_confirm_pid > 0 && sel_pid > 0 &&
        ui->kill_confirm_pid == sel_pid) {
        x = ui->win_w - PAD - btn_yes_w;
        yes->x = x;
        yes->y = dy + 4;
        yes->w = btn_yes_w;
        yes->h = BTN_H;

        no->x = x - CARD_GAP - btn_no_w;
        no->y = dy + 4;
        no->w = btn_no_w;
        no->h = BTN_H;

        kill->w = 0;
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
    ui->filter_focus = 0;
    ui->kill_confirm_pid = -1;
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

void ui_draw(Ui *ui, const PortRow *visible, size_t visible_count,
             const ProcessDetail *detail, const char *status_line)
{
    SDL_Renderer *r = ui->renderer;
    int y0 = table_top_y();
    int thead_y = y0;
    int tbody_y = y0 + TABLE_HEADER_H;
    int col_pid = PAD;
    int col_proto = PAD + 72;
    int col_state = PAD + 130;
    int col_comm = PAD + 220;
    int col_name = PAD + 360;
    int i;
    int max_rows;
    int row0;
    size_t idx;
    int sel_pid = -1;

    if (ui->selected_visible >= 0 &&
        (size_t)ui->selected_visible < visible_count)
        sel_pid = visible[ui->selected_visible].pid;

    SDL_GetWindowSize(ui->window, &ui->win_w, &ui->win_h);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, COL_BG.r, COL_BG.g, COL_BG.b, 255);
    SDL_RenderClear(r);

    draw_rect(r, (SDL_Rect){0, 0, ui->win_w, ui->win_h}, COL_BG);

    /* Filter row */
    draw_rect(r, (SDL_Rect){0, 0, ui->win_w, FILTER_H}, COL_BG);
    draw_text(r, ui->font, "Filter:", PAD, 8, COL_DIM);
    {
        SDL_Rect box = {PAD + 56, 6, ui->win_w - PAD * 2 - 56, FILTER_BOX_H};
        draw_rect(r, box, COL_PANEL);
        if (ui->filter_focus) {
            SDL_SetRenderDrawColor(r, COL_ACCENT.r, COL_ACCENT.g, COL_ACCENT.b,
                                   255);
            SDL_RenderDrawRect(r, &box);
        }
        draw_text(r, ui->font, ui->filter, box.x + 6, box.y + 4, COL_TEXT);
    }

    /* Table header */
    draw_rect(r, (SDL_Rect){0, thead_y, ui->win_w, TABLE_HEADER_H}, COL_PANEL);
    draw_text(r, ui->font, "PID", col_pid, thead_y + 4, COL_ACCENT);
    draw_text(r, ui->font, "Proto", col_proto, thead_y + 4, COL_ACCENT);
    draw_text(r, ui->font, "State", col_state, thead_y + 4, COL_ACCENT);
    draw_text(r, ui->font, "Command", col_comm, thead_y + 4, COL_ACCENT);
    draw_text(r, ui->font, "Socket (local / peer)", col_name, thead_y + 4,
              COL_ACCENT);

    max_rows = table_area_h(ui) / ROW_H;
    if (max_rows < 1)
        max_rows = 1;

    if (ui->table_scroll < 0)
        ui->table_scroll = 0;
    {
        int total_h = (int)visible_count * ROW_H;
        int max_scroll = total_h - table_area_h(ui);
        if (max_scroll < 0)
            max_scroll = 0;
        if (ui->table_scroll > max_scroll)
            ui->table_scroll = max_scroll;
    }

    row0 = ui->table_scroll / ROW_H;
    if (row0 < 0)
        row0 = 0;

    SDL_RenderSetClipRect(r, &(SDL_Rect){0, tbody_y, ui->win_w, table_area_h(ui)});

    for (i = 0; i < max_rows; i++) {
        idx = (size_t)row0 + (size_t)i;
        if (idx >= visible_count)
            break;
        {
            int ry = tbody_y + i * ROW_H - (ui->table_scroll % ROW_H);
            SDL_Color rowbg = (idx % 2) ? COL_ZEBRA_B : COL_ZEBRA_A;
            char pidbuf[32];

            if ((int)idx == ui->selected_visible)
                rowbg = COL_SELECT;

            draw_rect(r, (SDL_Rect){0, ry, ui->win_w, ROW_H}, rowbg);

            snprintf(pidbuf, sizeof(pidbuf), "%d", visible[idx].pid);
            draw_text(r, ui->font, pidbuf, col_pid, ry + 2, COL_TEXT);
            draw_text_clamp(r, ui->font, visible[idx].proto, col_proto, ry + 2,
                            90, COL_TEXT);
            draw_text_clamp(r, ui->font, visible[idx].state, col_state, ry + 2,
                            80, COL_TEXT);
            draw_text_clamp(r, ui->font, visible[idx].comm, col_comm, ry + 2,
                            130, COL_TEXT);
            draw_text_clamp(r, ui->font, visible[idx].name, col_name, ry + 2,
                            ui->win_w - col_name - PAD, COL_TEXT);
        }
    }

    SDL_RenderSetClipRect(r, NULL);

    /* Detail pane — card layout */
    {
        int dy = detail_pane_y(ui);
        int row1y = dy + HEADER_ROW;
        int gap = CARD_GAP;
        int card_w = (ui->win_w - 2 * PAD - 3 * gap) / 4;
        int cx = PAD;
        SDL_Rect r_kill, r_yes, r_no;

        draw_rect(r, (SDL_Rect){0, dy, ui->win_w, DETAIL_H}, COL_PANEL);
        draw_rect_outline(r, (SDL_Rect){0, dy, ui->win_w, DETAIL_H}, COL_DIM);

        draw_text(r, ui->font, "Process detail", PAD, dy + 8, COL_ACCENT);
        if (status_line && status_line[0])
            draw_text_clamp(r, ui->font, status_line, PAD + 130, dy + 8,
                            ui->win_w - PAD * 2 - 300, COL_DIM);

        detail_kill_button_layout(ui, sel_pid, &r_kill, &r_yes, &r_no);
        if (r_yes.w > 0) {
            draw_button(r, ui->font, r_yes, "Yes, send SIGTERM", COL_DANGER_BG,
                        COL_DANGER, COL_TEXT);
            draw_button(r, ui->font, r_no, "Cancel", COL_BTN_BG, COL_ACCENT,
                        COL_TEXT);
        } else if (sel_pid > 0) {
            draw_button(r, ui->font, r_kill, "Kill (SIGTERM)", COL_DANGER_BG,
                        COL_DANGER, COL_TEXT);
        }

        if (!detail || !detail->valid) {
            const char *msg =
                (detail && detail->err[0]) ? detail->err
                                           : "Select a socket row to inspect.";
            draw_text(r, ui->font, msg, PAD, row1y + 8, COL_DIM);
        } else {
            char pidbuf[32];

            snprintf(pidbuf, sizeof(pidbuf), "%d", detail->pid);
            draw_card(r, ui->font, cx, row1y, card_w, MINI_CARD_H, "PID",
                      pidbuf, COL_DIM, COL_TEXT);
            cx += card_w + gap;
            draw_card(r, ui->font, cx, row1y, card_w, MINI_CARD_H, "UID",
                      detail->uid[0] ? detail->uid : "—", COL_DIM, COL_TEXT);
            cx += card_w + gap;
            draw_card(r, ui->font, cx, row1y, card_w, MINI_CARD_H, "GID",
                      detail->gid[0] ? detail->gid : "—", COL_DIM, COL_TEXT);
            cx += card_w + gap;
            draw_card(r, ui->font, cx, row1y, card_w, MINI_CARD_H, "Elapsed",
                      detail->etime[0] ? detail->etime : "—", COL_DIM, COL_TEXT);

            {
                int ny = row1y + MINI_CARD_H + gap;
                draw_card(r, ui->font, PAD, ny, ui->win_w - 2 * PAD,
                          NAME_CARD_H, "Name (kernel comm)", detail->name, COL_DIM,
                          COL_TEXT);
                ny += NAME_CARD_H + gap;
                {
                    SDL_Rect cmdbox = {PAD, ny, ui->win_w - 2 * PAD,
                                       dy + DETAIL_H - ny - PAD};
                    draw_rect(r, cmdbox, COL_CARD);
                    draw_rect_outline(r, cmdbox, COL_DIM);
                    draw_text(r, ui->font, "Command line", cmdbox.x + 6,
                              cmdbox.y + 4, COL_DIM);
                    draw_text_wrapped(
                        r, ui->font, detail->cmdline, cmdbox.x + 6,
                        cmdbox.y + 4 + ROW_H, cmdbox.w - 12,
                        cmdbox.y + cmdbox.h - 4, CMD_LINE_H, COL_TEXT);
                }
            }
        }
    }
}

int ui_handle_event(Ui *ui, const SDL_Event *e, const PortRow *visible,
                    size_t visible_count, int *out_table_top_y)
{
    int tbody_y;
    int tah;
    int sel_pid = -1;

    if (ui->window)
        SDL_GetWindowSize(ui->window, &ui->win_w, &ui->win_h);

    if (ui->selected_visible >= 0 &&
        (size_t)ui->selected_visible < visible_count)
        sel_pid = visible[ui->selected_visible].pid;

    tbody_y = table_top_y() + TABLE_HEADER_H;
    tah = table_area_h(ui);

    if (out_table_top_y)
        *out_table_top_y = table_top_y();

    switch (e->type) {
    case SDL_QUIT:
        return 1;
    case SDL_KEYDOWN:
        if (ui->filter_focus) {
            if (e->key.keysym.sym == SDLK_ESCAPE) {
                ui->filter_focus = 0;
                SDL_StopTextInput();
                return 0;
            }
            if (e->key.keysym.sym == SDLK_BACKSPACE) {
                size_t n = strlen(ui->filter);
                if (n > 0) {
                    ui->filter[n - 1] = '\0';
                    return 3;
                }
                return 0;
            }
            if (e->key.keysym.sym == SDLK_RETURN ||
                e->key.keysym.sym == SDLK_KP_ENTER) {
                ui->filter_focus = 0;
                SDL_StopTextInput();
                return 0;
            }
            return 0;
        }
        if (ui->kill_confirm_pid > 0 && e->key.keysym.sym == SDLK_ESCAPE) {
            ui->kill_confirm_pid = -1;
            return 0;
        }
        if (e->key.keysym.sym == SDLK_F5)
            return 2;
        if (e->key.keysym.sym == SDLK_ESCAPE)
            return 1;
        if (e->key.keysym.sym == SDLK_f &&
            (e->key.keysym.mod & KMOD_CTRL)) {
            ui->filter_focus = 1;
            SDL_StartTextInput();
            return 0;
        }
        return 0;
    case SDL_TEXTINPUT:
        if (ui->filter_focus) {
            size_t n = strlen(ui->filter);
            size_t add = strlen(e->text.text);
            if (n + add < sizeof(ui->filter) - 1) {
                memcpy(ui->filter + n, e->text.text, add + 1);
                return 3;
            }
        }
        return 0;
    case SDL_MOUSEWHEEL:
        if (!ui->filter_focus) {
            int delta = e->wheel.y * ROW_H * 3;
            ui->table_scroll -= delta;
            return 0;
        }
        return 0;
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button == SDL_BUTTON_LEFT) {
            int mx = e->button.x;
            int my = e->button.y;
            SDL_Rect filter_box = {PAD + 56, 6, ui->win_w - PAD * 2 - 56,
                                   FILTER_BOX_H};
            SDL_Rect r_kill, r_yes, r_no;

            if (my >= 0 && my < FILTER_H && mx >= filter_box.x &&
                mx < filter_box.x + filter_box.w) {
                ui->filter_focus = 1;
                SDL_StartTextInput();
                return 0;
            }
            ui->filter_focus = 0;
            SDL_StopTextInput();

            if (my >= detail_pane_y(ui)) {
                detail_kill_button_layout(ui, sel_pid, &r_kill, &r_yes, &r_no);
                if (r_yes.w > 0) {
                    if (pt_in_rect(mx, my, r_yes)) {
                        if (sel_pid > 0 && ui->kill_confirm_pid == sel_pid)
                            return 5;
                        return 0;
                    }
                    if (pt_in_rect(mx, my, r_no)) {
                        ui->kill_confirm_pid = -1;
                        return 0;
                    }
                } else if (r_kill.w > 0 && pt_in_rect(mx, my, r_kill)) {
                    if (sel_pid > 0)
                        ui->kill_confirm_pid = sel_pid;
                    return 0;
                }
                return 0;
            }

            if (my >= tbody_y && my < tbody_y + tah) {
                int rel = my - tbody_y + ui->table_scroll;
                int row = rel / ROW_H;
                if (row >= 0 && (size_t)row < visible_count) {
                    ui->selected_visible = row;
                    ui->kill_confirm_pid = -1;
                    return 4;
                }
            }
            ui->selected_visible = -1;
            ui->kill_confirm_pid = -1;
            return 4;
        }
        return 0;
    default:
        return 0;
    }
}
