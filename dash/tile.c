#include "tile.h"

#include <stdlib.h>
#include <string.h>

void tile_free(Tile *t)
{
    int i;

    if (!t)
        return;
    free(t->title);
    free(t->desc);
    if (t->argv) {
        for (i = 0; t->argv[i]; i++)
            free(t->argv[i]);
        free(t->argv);
    }
    memset(t, 0, sizeof(*t));
}
