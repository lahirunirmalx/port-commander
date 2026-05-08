#ifndef APCOMPAT_H
#define APCOMPAT_H

#include <stddef.h>

/*
 * Tiny platform-abstraction layer for the dashboard.
 *
 * The dashboard needs three things that are platform-specific:
 *   - "where do I live on disk" (to find sibling tool binaries)
 *   - "spawn a child without blocking, then poll for its exit"
 *   - "does this path point at something I can execute"
 *
 * Everything else in the project (`portcommander`, `wificommander`, the
 * imported instrument tools) is intrinsically POSIX/Linux and is gated at
 * the CMake level, not behind this layer. If/when a Windows port of those
 * tools is written, the corresponding CMake gate is the place to update.
 *
 * Implementation: POSIX (Linux/macOS) is live. Windows path is sketched
 * with `#ifdef _WIN32` and uses the Win32 process APIs; it has not been
 * compiled and will need MinGW or MSVC headers + SDL2 from vcpkg/etc.
 */

/* Opaque process handle. */
typedef struct CompatProc CompatProc;

/*
 * Writes the directory containing the running executable into `out`
 * (with no trailing separator). Returns 0 on success, -1 on failure
 * (out is then set to "."). Uses SDL_GetBasePath() under the hood, which
 * is portable.
 */
int compat_app_dir(char *out, size_t outsz);

/* Returns ".exe" on Windows, "" elsewhere. */
const char *compat_exe_suffix(void);

/*
 * Returns 1 if `path` looks runnable (POSIX: access(X_OK); Windows: file
 * exists), 0 otherwise. Used to gray out dashboard tiles whose binary is
 * missing.
 */
int compat_can_execute(const char *path);

/*
 * Spawns the executable at `path` and returns immediately. If `argv` is
 * non-NULL, it is a NULL-terminated array of arguments passed to the
 * child (argv[0] is conventionally the program name and is what the
 * child sees as its argv[0]). If `argv` is NULL, the child is invoked
 * with the single-element argv `{path, NULL}` for back-compat.
 *
 * Returns a non-NULL handle on success; returns NULL on failure with
 * errmsg filled (errmsg may be NULL).
 *
 * Inherits the parent's stdin/stdout/stderr — child tool windows are
 * separate SDL windows, so terminal output (if any) goes to the parent's
 * terminal.
 */
CompatProc *compat_spawn(const char *path, char *const argv[],
                         char *errmsg, size_t errsz);

/*
 * Searches $PATH for an executable named `name` (which must not contain
 * a path separator). On success, writes the absolute path to `out` and
 * returns 0. Returns -1 if `name` isn't found, isn't executable, or the
 * arguments are invalid.
 */
int compat_path_lookup(const char *name, char *out, size_t outsz);

/*
 * Polls the child without blocking.
 *   Returns 1 if the child has exited (and writes exit code via
 *     out_exit_code if non-NULL).
 *   Returns 0 if the child is still running.
 *   Returns -1 on error.
 *
 * After a return of 1, subsequent calls keep returning 1 with the same
 * exit code until compat_proc_free() is called.
 */
int compat_proc_poll(CompatProc *p, int *out_exit_code);

/*
 * Releases the handle. Does NOT terminate the child if still running —
 * the dashboard treats children as detachable.
 */
void compat_proc_free(CompatProc *p);

#endif
