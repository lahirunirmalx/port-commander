#ifndef PORTCMD_SPAWN_UTIL_H
#define PORTCMD_SPAWN_UTIL_H

#include <stdio.h>
#include <sys/types.h>

/*
 * Tiny fork/pipe/execv helper for portcommander. Replaces popen() so:
 *   1. We avoid invoking /bin/sh -c, which keeps the launched binary
 *      out of $PATH-resolution. Important when the dashboard is run
 *      under sudo without sudo's `secure_path` enabled — a user-owned
 *      directory earlier on $PATH could otherwise plant a malicious
 *      `lsof`/`ps` that runs as root.
 *   2. We can drain and close the read end on a row-cap break without
 *      waiting for the child to finish writing, avoiding the pclose
 *      hang that popen requires.
 */

/*
 * Resolves a bare command name (e.g. "lsof") to an absolute path by
 * checking a fixed list of system locations in priority order. The
 * project explicitly does NOT walk $PATH here. Writes the absolute
 * path to `out` and returns 0 on success; returns -1 if none of the
 * candidates is executable (`out` set to "").
 */
int spawn_resolve_abs(const char *name, char *out, size_t outsz);

/*
 * Forks and execs `abs_path` (must be absolute) with `argv`. Returns
 * a FILE* opened on the child's stdout (caller fcloses), or NULL on
 * failure. stderr is redirected to /dev/null. Inherited file
 * descriptors above stderr are closed in the child before execv.
 *
 * `*out_pid` is filled with the child's PID on success — reap with
 * spawn_reap() after the FILE* is closed.
 */
FILE *spawn_capture_stdout(const char *abs_path, char *const argv[],
                           pid_t *out_pid);

/* Waits for `pid`; returns the child's exit code or -1 on signal/error. */
int spawn_reap(pid_t pid);

#endif
