#ifndef NMCLI_RUN_H
#define NMCLI_RUN_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

/*
 * Forks and execvps argv[0]. Returns a FILE* for the child's stdout (caller
 * fcloses), or NULL on failure. stderr is redirected to /dev/null so query
 * commands don't leak warnings into our parsing. Sets *out_pid to the child
 * pid; reap with nmcli_reap() after closing the FILE*.
 */
FILE *nmcli_spawn_stdout(char *const argv[], pid_t *out_pid);

/* Waits for pid; returns child's exit code, or -1 on signal/error. */
int nmcli_reap(pid_t pid);

/*
 * Forks and execvps argv[0], capturing the child's stderr into err. stdout
 * is sent to /dev/null. Returns 0 if the child exits with code 0 (err is
 * cleared). On non-zero exit, returns -1 and err contains the captured
 * stderr text (or a fallback diagnostic if stderr was empty).
 */
int nmcli_run_capture_stderr(char *const argv[], char *err, size_t errsz);

#endif
