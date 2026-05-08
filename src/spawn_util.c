#define _GNU_SOURCE
#include "spawn_util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Trusted absolute paths checked when resolving a command. Order
 * matters: the first existing executable wins. Keep this list short
 * and only include directories that should be system-administered.
 */
static const char *const SYSTEM_BIN_DIRS[] = {
    "/usr/bin",
    "/bin",
    "/usr/sbin",
    "/sbin",
    NULL,
};

int spawn_resolve_abs(const char *name, char *out, size_t outsz)
{
    int i;

    if (!name || !out || outsz == 0)
        return -1;
    out[0] = '\0';
    /* Reject anything that already looks like a path — callers should
     * either pass a bare name or an absolute path, not a relative one. */
    if (strchr(name, '/'))
        return -1;
    for (i = 0; SYSTEM_BIN_DIRS[i]; i++) {
        char candidate[1024];
        int w = snprintf(candidate, sizeof(candidate), "%s/%s",
                         SYSTEM_BIN_DIRS[i], name);
        if (w < 0 || (size_t)w >= sizeof(candidate))
            continue;
        if (access(candidate, X_OK) == 0) {
            snprintf(out, outsz, "%s", candidate);
            return 0;
        }
    }
    return -1;
}

/*
 * Close every inherited file descriptor above stderr before exec so
 * the child doesn't hold the dashboard's X11 socket, font mmaps, or
 * anything else.
 */
static void close_inherited_fds(void)
{
    struct rlimit rl;
    int max_fd;
    int fd;

    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
        max_fd = (int)rl.rlim_cur;
    else
        max_fd = 1024;
    for (fd = 3; fd < max_fd; fd++)
        (void)close(fd);
}

FILE *spawn_capture_stdout(const char *abs_path, char *const argv[],
                           pid_t *out_pid)
{
    int p[2];
    pid_t pid;
    FILE *fp;

    if (!abs_path || abs_path[0] != '/' || !argv || !argv[0] || !out_pid)
        return NULL;
    if (pipe(p) < 0)
        return NULL;

    pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return NULL;
    }
    if (pid == 0) {
        int devnull;

        close(p[0]);
        if (dup2(p[1], STDOUT_FILENO) < 0)
            _exit(127);
        devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        close(p[1]);
        close_inherited_fds();
        /* execv (not execvp) — we already have the absolute path. No
         * $PATH walk, no /bin/sh involvement. */
        execv(abs_path, argv);
        _exit(127);
    }
    close(p[1]);
    fp = fdopen(p[0], "r");
    if (!fp) {
        int st;

        close(p[0]);
        waitpid(pid, &st, 0);
        return NULL;
    }
    *out_pid = pid;
    return fp;
}

int spawn_reap(pid_t pid)
{
    int status;

    if (waitpid(pid, &status, 0) < 0) {
        if (errno == ECHILD)
            return -1; /* already reaped */
        return -1;
    }
    if (!WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
}
