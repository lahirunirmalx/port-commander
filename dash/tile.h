#ifndef DASH_TILE_H
#define DASH_TILE_H

#include <SDL.h>
#include <limits.h>

#ifndef PATH_MAX
#  define PATH_MAX 1024
#endif

/*
 * One launcher entry as seen by the dashboard. A Tile is created by
 * the config layer (see config.h) and consumed by the menu layer
 * (which writes `rect` and `hover` each frame) and the spawn pool
 * (which updates `running_count` and `last_launch_ms`).
 *
 * Memory ownership: title, desc, and the argv array (and each string
 * inside it) are heap-allocated and owned by the Tile. Call
 * tile_free() before the Tile goes out of scope.
 */
typedef struct Tile {
    SDL_Rect rect;          /* recomputed each frame by the menu layer  */
    char *title;            /* owned (strdup); never NULL after load    */
    char *desc;             /* owned (strdup); may be NULL              */
    char exe_path[PATH_MAX];/* resolved absolute path to the executable */
    char **argv;            /* owned, NULL-terminated; argv[0] == path  */
    int argc;
    int hover;              /* 1 if the mouse is over this tile         */
    int missing;            /* 1 if exe_path isn't currently executable */
    int running_count;      /* number of currently-alive children       */
    Uint32 last_launch_ms;  /* SDL_GetTicks() at last successful launch */
    char hotkey;            /* single printable char; 0 == none         */
} Tile;

/*
 * Releases every owned allocation inside the Tile and zeroes the
 * struct. Safe to call on a zero-initialised Tile.
 */
void tile_free(Tile *t);

#endif
