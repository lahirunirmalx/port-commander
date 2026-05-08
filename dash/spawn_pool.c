#include "spawn_pool.h"

#include "compat.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * How long after a launch a second launch of the same tile is dropped.
 * Long enough to swallow OS key-repeat (typically 250–500 ms) and a
 * stray double-click after a spawned window grabs focus, short enough
 * that intentionally relaunching a tool feels responsive.
 */
#define LAUNCH_DEBOUNCE_MS 350

typedef struct Child {
    CompatProc *proc;
    int tile_index;
} Child;

struct SpawnPool {
    Child *children;
    int count;
    int capacity;
};

SpawnPool *pool_create(int max_children)
{
    SpawnPool *p;

    if (max_children <= 0)
        return NULL;
    p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->children = calloc((size_t)max_children, sizeof(*p->children));
    if (!p->children) {
        free(p);
        return NULL;
    }
    p->capacity = max_children;
    return p;
}

void pool_destroy_detach(SpawnPool *p)
{
    int i;

    if (!p)
        return;
    /* Free the OS handles, but do NOT signal/terminate the children —
     * they should outlive the dashboard. POSIX: parent dies, init
     * adopts. Windows: the inherited handles get closed and the child
     * keeps running until its own event loop exits. */
    for (i = 0; i < p->count; i++)
        compat_proc_free(p->children[i].proc);
    free(p->children);
    free(p);
}

int pool_launch(SpawnPool *p, Tile *tiles, int idx,
                char *errmsg, size_t errmsg_sz)
{
    Tile *t;
    CompatProc *proc;
    Uint32 now;

    if (errmsg && errmsg_sz)
        errmsg[0] = '\0';
    if (!p || !tiles || idx < 0)
        return -1;
    t = &tiles[idx];
    now = SDL_GetTicks();

    /* Debounce: SDL emits KEYDOWN on initial press AND on OS auto-repeat
     * (~250–500 ms in), and a launched window stealing focus tempts the
     * user to "click again" almost instantly. Drop a second launch of
     * the same tile inside the window. SDL_GetTicks wraps after ~49
     * days — Uint32 modular subtraction gives the right answer across
     * that wrap. */
    if (t->last_launch_ms != 0 &&
        (Uint32)(now - t->last_launch_ms) < (Uint32)LAUNCH_DEBOUNCE_MS)
        return 0;

    /* Re-check at click time — closes the TOCTOU window if the binary
     * was removed, swapped, or chmod'd between dashboard startup and
     * now. */
    t->missing = !compat_can_execute(t->exe_path);
    if (t->missing) {
        if (errmsg)
            snprintf(errmsg, errmsg_sz, "Not found: %s", t->exe_path);
        return -1;
    }

    if (p->count >= p->capacity) {
        if (errmsg)
            snprintf(errmsg, errmsg_sz,
                     "Already running %d tools — close one first.",
                     p->capacity);
        return -1;
    }

    proc = compat_spawn(t->exe_path, t->argv, errmsg, errmsg_sz);
    if (!proc) {
        /* Spawn failed; refresh the missing flag so the tile reflects
         * current reality. */
        t->missing = !compat_can_execute(t->exe_path);
        return -1;
    }

    p->children[p->count].proc = proc;
    p->children[p->count].tile_index = idx;
    p->count++;
    t->running_count++;
    t->last_launch_ms = now ? now : 1; /* keep 0 reserved for "never" */
    return 0;
}

void pool_reap(SpawnPool *p, Tile *tiles, int num_tiles)
{
    int i;

    if (!p)
        return;

    /*
     * A poll error (-1) is treated as terminal: keep one bad handle
     * around across many frames and on Windows the leaked HANDLE
     * accumulates fast. Better to drop the entry, free the handle, and
     * surface the count drop.
     */
    i = 0;
    while (i < p->count) {
        int code = 0;
        int r = compat_proc_poll(p->children[i].proc, &code);

        if (r != 0) { /* exited (1) or errored (-1) */
            int idx = p->children[i].tile_index;

            if (idx >= 0 && idx < num_tiles &&
                tiles[idx].running_count > 0)
                tiles[idx].running_count--;
            compat_proc_free(p->children[i].proc);
            p->children[i] = p->children[p->count - 1];
            p->count--;
        } else {
            i++;
        }
    }
}

int pool_total(const SpawnPool *p)
{
    return p ? p->count : 0;
}

int pool_capacity(const SpawnPool *p)
{
    return p ? p->capacity : 0;
}
