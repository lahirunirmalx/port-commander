#include "render.h"

const SDL_Color COL_BG         = {  26,  28,  34, 255 };
const SDL_Color COL_CARD       = {  48,  50,  62, 255 };
const SDL_Color COL_CARD_HOVER = {  60,  86, 124, 255 };
const SDL_Color COL_CARD_SEL   = {  70, 110, 160, 255 };
const SDL_Color COL_CARD_RUN   = {  44,  70,  56, 255 };
const SDL_Color COL_TEXT       = { 230, 232, 238, 255 };
const SDL_Color COL_DIM        = { 150, 154, 168, 255 };
const SDL_Color COL_ACCENT     = { 100, 180, 255, 255 };
const SDL_Color COL_GOOD       = { 120, 200, 130, 255 };
const SDL_Color COL_DANGER     = { 200,  90,  90, 255 };
const SDL_Color COL_CURVE      = {  70,  74,  88, 255 };

void render_fill(SDL_Renderer *r, SDL_Rect rect, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rect);
}

void render_outline(SDL_Renderer *r, SDL_Rect rect, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(r, &rect);
}

void render_text(SDL_Renderer *r, TTF_Font *font, const char *text, int x,
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

void render_text_centered(SDL_Renderer *r, TTF_Font *font, const char *text,
                          int x_center, int y, SDL_Color fg)
{
    int tw = 0;
    int th = 0;

    if (!text || !text[0])
        return;
    if (TTF_SizeUTF8(font, text, &tw, &th) != 0)
        return;
    render_text(r, font, text, x_center - tw / 2, y, fg);
}

int render_pt_in_rect(int mx, int my, SDL_Rect r)
{
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}
