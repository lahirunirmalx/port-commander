#define _GNU_SOURCE
#include "qr_codec.h"
#include "nmcli_run.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

static int wifi_escape(char *dst, size_t dstsz, const char *src)
{
    size_t off = 0;

    if (!dst || dstsz == 0)
        return -1;
    while (src && *src) {
        char c = *src++;

        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
            if (off + 2 >= dstsz) {
                dst[off] = '\0';
                return -1;
            }
            dst[off++] = '\\';
        } else if (off + 1 >= dstsz) {
            dst[off] = '\0';
            return -1;
        }
        dst[off++] = c;
    }
    dst[off] = '\0';
    return 0;
}

int qr_build_wifi(const char *ssid, const char *password, char *out,
                  size_t outsz)
{
    char esc_ssid[200];
    char esc_pwd[200];
    char payload[700];
    char *argv[8];
    pid_t pid = -1;
    FILE *fp;
    char buf[1024];
    size_t off = 0;
    int rc;

    if (out && outsz)
        out[0] = '\0';
    if (!out || outsz == 0 || !ssid || !ssid[0])
        return -1;

    if (wifi_escape(esc_ssid, sizeof(esc_ssid), ssid) != 0)
        return -1;
    if (wifi_escape(esc_pwd, sizeof(esc_pwd), password ? password : "") != 0)
        return -1;

    if (password && password[0])
        snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%s;P:%s;;", esc_ssid,
                 esc_pwd);
    else
        snprintf(payload, sizeof(payload), "WIFI:T:nopass;S:%s;;", esc_ssid);

    argv[0] = "qrencode";
    argv[1] = "-t";
    argv[2] = "UTF8";
    argv[3] = "-o";
    argv[4] = "-";
    argv[5] = payload;
    argv[6] = NULL;

    fp = nmcli_spawn_stdout(argv, &pid);
    if (!fp)
        return -1;

    while (fgets(buf, sizeof(buf), fp)) {
        size_t n = strlen(buf);

        if (off + n + 1 >= outsz)
            break;
        memcpy(out + off, buf, n);
        off += n;
    }
    while (fgets(buf, sizeof(buf), fp)) { /* drain */ }
    fclose(fp);
    rc = nmcli_reap(pid);

    if (off > 0)
        out[off] = '\0';
    return (rc == 0 && off > 0) ? 0 : -1;
}

void qr_text_dims(const char *qr_text, int *cols, int *text_rows)
{
    int c = 0;
    int rows = 0;
    int max_c = 0;
    const unsigned char *p = (const unsigned char *)qr_text;

    if (cols)
        *cols = 0;
    if (text_rows)
        *text_rows = 0;
    if (!qr_text)
        return;

    while (*p) {
        if (*p == '\n') {
            if (c > max_c)
                max_c = c;
            rows++;
            c = 0;
            p++;
            continue;
        }
        if (*p == '\r') {
            p++;
            continue;
        }
        if (*p == ' ') {
            c++;
            p++;
            continue;
        }
        if (p[0] == 0xE2 && p[1] == 0x96 &&
            (p[2] == 0x80 || p[2] == 0x84 || p[2] == 0x88)) {
            c++;
            p += 3;
            continue;
        }
        p++;
    }
    if (c > max_c)
        max_c = c;
    if (cols)
        *cols = max_c;
    if (text_rows)
        *text_rows = rows;
}

void qr_render(SDL_Renderer *r, int x, int y, int module_px,
               const char *qr_text)
{
    int row = 0;
    int col = 0;
    const unsigned char *p = (const unsigned char *)qr_text;

    if (!qr_text || !*qr_text || module_px <= 0)
        return;

    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);

    while (*p) {
        int top_black = 0;
        int bot_black = 0;
        int draw_this = 1;
        int advance = 1;

        if (*p == '\n') {
            row += 2;
            col = 0;
            p++;
            continue;
        }
        if (*p == '\r') {
            p++;
            continue;
        }

        if (*p == ' ') {
            top_black = 1;
            bot_black = 1;
        } else if (p[0] == 0xE2 && p[1] == 0x96) {
            switch (p[2]) {
            case 0x80: /* upper-half block: rendered as top white, bot black */
                bot_black = 1;
                advance = 3;
                break;
            case 0x84: /* lower-half block: top black, bot white */
                top_black = 1;
                advance = 3;
                break;
            case 0x88: /* full block: both white */
                advance = 3;
                break;
            default:
                draw_this = 0;
                advance = 1;
                break;
            }
        } else {
            draw_this = 0;
        }

        if (draw_this) {
            if (top_black) {
                SDL_Rect rr = { x + col * module_px, y + row * module_px,
                                module_px, module_px };

                SDL_RenderFillRect(r, &rr);
            }
            if (bot_black) {
                SDL_Rect rr = { x + col * module_px,
                                y + (row + 1) * module_px, module_px,
                                module_px };

                SDL_RenderFillRect(r, &rr);
            }
            col++;
        }
        p += advance;
    }
}
