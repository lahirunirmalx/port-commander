#ifndef QR_CODEC_H
#define QR_CODEC_H

#include <SDL.h>
#include <stddef.h>

/*
 * Builds a Wi-Fi credential payload (WIFI:T:WPA;S:<ssid>;P:<password>;;)
 * with the proper escaping for ';', ':', ',', '\\', '"', then runs
 * `qrencode -t UTF8` and captures its output into out (a buffer of qrencode's
 * own UTF-8 block-character QR rendering).
 *
 * Returns 0 on success, -1 if qrencode isn't installed or fails. The caller
 * should treat -1 as "no QR available" and skip rendering.
 *
 * If password is NULL or empty, an open ("nopass") payload is generated.
 */
int qr_build_wifi(const char *ssid, const char *password, char *out,
                  size_t outsz);

/*
 * Returns the qr_text grid dimensions in modules. cols is module-width
 * (one module per UTF-8 block character or space). text_rows is the number
 * of '\n'-terminated lines (each line covers 2 module-rows vertically).
 */
void qr_text_dims(const char *qr_text, int *cols, int *text_rows);

/*
 * Renders qrencode's UTF-8 block-character QR as filled black rectangles
 * at (x, y). Caller must paint the white background separately.
 *
 * module_px is the side length in pixels of each QR module.
 *
 * Conversion (qrencode -t UTF8 is designed for white-on-black terminals,
 * so we invert):
 *   ' ' (space)           -> two black modules (top + bottom)
 *   '▀' upper-half   -> bottom module black
 *   '▄' lower-half   -> top module black
 *   '█' full block   -> no draw (white)
 */
void qr_render(SDL_Renderer *r, int x, int y, int module_px,
               const char *qr_text);

#endif
