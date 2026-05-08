#ifndef DASH_CONFIG_H
#define DASH_CONFIG_H

#include "tile.h"

#include <stddef.h>

/*
 * Searches the standard config locations and loads the first config
 * file it finds. Search order (highest priority first):
 *   1. cli_path (argv[1] / explicit override)
 *   2. $APCOMMANDER_CONFIG environment variable
 *   3. $XDG_CONFIG_HOME/apcommander/tiles.conf
 *      (or ~/.config/apcommander/tiles.conf if XDG_CONFIG_HOME unset)
 *   4. <app_dir>/apcommander.conf
 *   5. <app_dir>/apcommander.default.conf
 *
 * Returns the number of tiles loaded into `tiles[0..max-1]`, or -1 if
 * no config file was found at any of the search locations. On success,
 * writes the absolute path of the chosen config file into `chosen_path`
 * for the dashboard to display in its footer.
 *
 * Each successfully populated Tile owns its strings — call tile_free()
 * on every tile (including those past the returned count is fine since
 * they're zeroed) before the array goes out of scope.
 */
int config_load(const char *cli_path, Tile *tiles, int max,
                char *chosen_path, size_t chosen_path_sz);

#endif
