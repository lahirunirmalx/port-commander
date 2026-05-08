#ifndef DASH_SPAWN_POOL_H
#define DASH_SPAWN_POOL_H

#include "tile.h"

#include <stddef.h>

/*
 * Tracks running child processes spawned from the dashboard. Children
 * are launched non-blocking and reaped each frame; on shutdown the
 * pool is destroyed *without* terminating live children, so the user
 * can keep using the tools they opened.
 *
 * Single owner per dashboard instance — the pool itself isn't
 * thread-safe.
 */
typedef struct SpawnPool SpawnPool;

/* Allocates a pool with room for up to `max_children` simultaneous
 * children. Returns NULL on allocation failure or invalid input. */
SpawnPool *pool_create(int max_children);

/* Frees the pool's tracking storage but leaves the children running.
 * Safe to call with NULL. */
void pool_destroy_detach(SpawnPool *p);

/*
 * Launches `tiles[idx]`'s configured executable. On success, increments
 * the tile's running_count and stamps its last_launch_ms.
 *
 * Returns 0 on success, 0 silently if the launch was debounced (a
 * second launch of the same tile inside the debounce window — not an
 * error from the user's perspective), or -1 on failure with errmsg
 * filled.
 */
int pool_launch(SpawnPool *p, Tile *tiles, int idx,
                char *errmsg, size_t errmsg_sz);

/* Polls every tracked child non-blocking. For each that has exited,
 * decrements the owning tile's running_count and frees the handle. */
void pool_reap(SpawnPool *p, Tile *tiles, int num_tiles);

/* Live-children count and capacity, for the footer status line. */
int pool_total(const SpawnPool *p);
int pool_capacity(const SpawnPool *p);

#endif
