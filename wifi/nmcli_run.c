#define _GNU_SOURCE
#include "nmcli_run.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Close every inherited file descriptor above stderr before execvp so
 * the child doesn't hold the dashboard's X11 socket, font mmaps, or
 * anything else. Mirrors compat/compat.c's POSIX child cleanup.
 *
 * Closing a descriptor we don't own is a defined no-op (EBADF), so a
 * loop that runs "too far" is safe.
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

FILE *nmcli_spawn_stdout(char *const argv[], pid_t *out_pid)
{
    int p[2];
    pid_t pid;
    FILE *fp;

    if (!argv || !argv[0] || !out_pid)
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
        execvp(argv[0], argv);
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

FILE *nmcli_spawn_stdin_stdout(char *const argv[], const void *stdin_data,
                               size_t stdin_len, pid_t *out_pid)
{
    int in_pipe[2];
    int out_pipe[2];
    pid_t pid;
    FILE *fp;

    if (!argv || !argv[0] || !out_pid)
        return NULL;
    if (stdin_len > 0 && !stdin_data)
        return NULL;

    if (pipe(in_pipe) < 0)
        return NULL;
    if (pipe(out_pipe) < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return NULL;
    }

    pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return NULL;
    }
    if (pid == 0) {
        int devnull;

        close(in_pipe[1]);
        close(out_pipe[0]);
        if (dup2(in_pipe[0], STDIN_FILENO) < 0)
            _exit(127);
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0)
            _exit(127);
        devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        close(in_pipe[0]);
        close(out_pipe[1]);
        close_inherited_fds();
        execvp(argv[0], argv);
        _exit(127);
    }

    /* Parent: write stdin first (small enough to fit in PIPE_BUF — see
     * the API doc) then close that side so the child sees EOF. */
    close(in_pipe[0]);
    close(out_pipe[1]);
    if (stdin_len > 0) {
        const unsigned char *p = (const unsigned char *)stdin_data;
        size_t left = stdin_len;

        while (left > 0) {
            ssize_t w = write(in_pipe[1], p, left);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                break; /* child died or pipe broken */
            }
            p += (size_t)w;
            left -= (size_t)w;
        }
    }
    close(in_pipe[1]);

    fp = fdopen(out_pipe[0], "r");
    if (!fp) {
        int st;

        close(out_pipe[0]);
        waitpid(pid, &st, 0);
        return NULL;
    }
    *out_pid = pid;
    return fp;
}

int nmcli_reap(pid_t pid)
{
    int status;

    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (!WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
}

int nmcli_run_capture_stderr(char *const argv[], char *err, size_t errsz)
{
    int p[2];
    pid_t pid;
    FILE *fp;
    char buf[256];
    size_t off = 0;
    int status;

    if (errsz)
        err[0] = '\0';
    if (!argv || !argv[0]) {
        if (errsz)
            snprintf(err, errsz, "(no argv)");
        return -1;
    }
    if (pipe(p) < 0) {
        if (errsz)
            snprintf(err, errsz, "pipe failed: %s", strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        if (errsz)
            snprintf(err, errsz, "fork failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        int devnull;

        close(p[0]);
        if (dup2(p[1], STDERR_FILENO) < 0)
            _exit(127);
        devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }
        close(p[1]);
        close_inherited_fds();
        execvp(argv[0], argv);
        _exit(127);
    }
    close(p[1]);
    fp = fdopen(p[0], "r");
    if (!fp) {
        close(p[0]);
        waitpid(pid, &status, 0);
        if (errsz)
            snprintf(err, errsz, "fdopen failed");
        return -1;
    }
    while (errsz && off + 1 < errsz && fgets(buf, sizeof(buf), fp)) {
        size_t n = strlen(buf);

        if (off + n >= errsz - 1)
            n = errsz - 1 - off;
        memcpy(err + off, buf, n);
        off += n;
        err[off] = '\0';
    }
    /* Drain anything that didn't fit so the child isn't blocked on write. */
    while (fgets(buf, sizeof(buf), fp)) { /* discard */ }
    fclose(fp);
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        if (errsz)
            err[0] = '\0';
        return 0;
    }
    if (errsz) {
        while (off > 0 && (err[off - 1] == '\n' || err[off - 1] == '\r')) {
            off--;
            err[off] = '\0';
        }
        if (off == 0)
            snprintf(err, errsz, "command failed (exit %d)",
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    }
    return -1;
}
