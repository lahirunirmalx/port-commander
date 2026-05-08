#ifndef DASH_MENU_H
#define DASH_MENU_H

#include "tile.h"

#include <SDL.h>
#include <SDL_ttf.h>

/*
 * Curved vertical-stack selector menu.
 *
 * The geometry: a fixed selector outline at the window's vertical
 * centre, a stack of options above and below it, and an animated
 * scroll value that the selected option converges on. Each option's
 * horizontal position bends outward along a circular arc as it moves
 * away from the selector — cribbed from smoothui's
 * fixed_selector_v_stack_curved_menu example.
 *
 * Lifecycle:
 *   menu_init(state)              once at startup
 *   per frame:
 *     menu_handle_event(...)      for each SDL event
 *     menu_step(state)            advance the scroll spring one tick
 *     menu_layout(state, ...)     compute per-tile rects for this frame
 *     menu_render(state, ...)     draw the menu
 */

typedef enum {
    MENU_NONE = 0,
    MENU_QUIT,    /* user pressed Esc / Q                                 */
    MENU_LAUNCH,  /* user activated a tile — *out_index is the tile index */
} MenuAction;

typedef struct Menu {
    int selected_idx;
    float scroll_y;
} Menu;

void menu_init(Menu *m);

/*
 * Handles one SDL event. Returns the action the dashboard should take.
 * For MENU_LAUNCH, *out_index is set to the tile to launch (caller
 * is responsible for the actual spawn — we just report what was
 * activated). out_index may be NULL if the caller doesn't care.
 *
 * `tiles[]` is read-only here, except for the `hover` flag which is
 * updated on mouse motion.
 */
MenuAction menu_handle_event(Menu *m, const SDL_Event *e, Tile *tiles,
                             int n, int *out_index);

/* Advances the selection-tracking spring one frame. */
void menu_step(Menu *m);

/*
 * Computes per-tile screen rects for this frame. After this call,
 * tiles[i].rect is the rectangle to draw / hit-test against.
 */
void menu_layout(const Menu *m, Tile *tiles, int n, int win_w, int win_h);

/*
 * Renders the menu region: curve-guide arc, options (with the selected
 * one highlighted), and the fixed selector outline. The caller has
 * already cleared the framebuffer and drawn its header.
 */
void menu_render(const Menu *m, SDL_Renderer *r, TTF_Font *font_md,
                 TTF_Font *font_sm, const Tile *tiles, int n,
                 int win_w, int win_h);

#endif
