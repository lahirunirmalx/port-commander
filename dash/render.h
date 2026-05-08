#ifndef DASH_RENDER_H
#define DASH_RENDER_H

#include <SDL.h>
#include <SDL_ttf.h>

/*
 * Thin wrappers over SDL2 + SDL2_ttf draw calls used by every dashboard
 * surface. Centralised so the menu, the header/footer chrome, and any
 * future overlays render through the same code path (and so we have one
 * place to change if we ever swap the renderer).
 *
 * No state — every call is independent and can be reordered freely.
 */

void render_fill(SDL_Renderer *r, SDL_Rect rect, SDL_Color c);
void render_outline(SDL_Renderer *r, SDL_Rect rect, SDL_Color c);
void render_text(SDL_Renderer *r, TTF_Font *font, const char *text,
                 int x, int y, SDL_Color fg);
void render_text_centered(SDL_Renderer *r, TTF_Font *font, const char *text,
                          int x_center, int y, SDL_Color fg);

int render_pt_in_rect(int mx, int my, SDL_Rect r);

/*
 * Shared dark-theme palette. Defined as static-storage variables in
 * render.c — extern-declared here so any dashboard surface can compose
 * with the same colours without duplicating the constants.
 */
extern const SDL_Color COL_BG;
extern const SDL_Color COL_CARD;
extern const SDL_Color COL_CARD_HOVER;
extern const SDL_Color COL_CARD_SEL;
extern const SDL_Color COL_CARD_RUN;
extern const SDL_Color COL_TEXT;
extern const SDL_Color COL_DIM;
extern const SDL_Color COL_ACCENT;
extern const SDL_Color COL_GOOD;
extern const SDL_Color COL_DANGER;
extern const SDL_Color COL_CURVE;

#endif
