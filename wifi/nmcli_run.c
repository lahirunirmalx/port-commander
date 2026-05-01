#define _GNU_SOURCE
#include "nmcli_run.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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
