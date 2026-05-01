#define _GNU_SOURCE
#include "compat.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <sys/resource.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

const char *compat_exe_suffix(void)
{
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

int compat_app_dir(char *out, size_t outsz)
{
    char *base;
    size_t n;

    if (!out || outsz == 0)
        return -1;
    out[0] = '\0';
    base = SDL_GetBasePath();
    if (!base) {
        snprintf(out, outsz, ".");
        return -1;
    }
    n = strlen(base);
    /* SDL_GetBasePath always ends in a separator; strip it. */
    while (n > 0 && (base[n - 1] == '/' || base[n - 1] == '\\')) {
        base[n - 1] = '\0';
        n--;
    }
    if (n == 0)
        snprintf(out, outsz, ".");
    else
        snprintf(out, outsz, "%s", base);
    SDL_free(base);
    return 0;
}

int compat_can_execute(const char *path)
{
    if (!path || !path[0])
        return 0;
#ifdef _WIN32
    /* Windows has no executable bit; existence + .exe extension by
     * convention is the closest equivalent for our purposes. */
    return _access(path, 0) == 0;
#else
    return access(path, X_OK) == 0;
#endif
}

/* ------------------------------------------------------------------------- */

struct CompatProc {
#ifdef _WIN32
    HANDLE process;
#else
    pid_t pid;
#endif
    int exited;
    int exit_code;
};

CompatProc *compat_spawn(const char *path, char *errmsg, size_t errsz)
{
    CompatProc *p;

    if (errmsg && errsz)
        errmsg[0] = '\0';
    if (!path || !path[0]) {
        if (errmsg)
            snprintf(errmsg, errsz, "compat_spawn: empty path");
        return NULL;
    }
    p = calloc(1, sizeof(*p));
    if (!p) {
        if (errmsg)
            snprintf(errmsg, errsz, "out of memory");
        return NULL;
    }

#ifdef _WIN32
    {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        char cmdline[1024];

        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);
        snprintf(cmdline, sizeof(cmdline), "\"%s\"", path);
        if (!CreateProcessA(path, cmdline, NULL, NULL, FALSE, 0, NULL, NULL,
                            &si, &pi)) {
            if (errmsg)
                snprintf(errmsg, errsz, "CreateProcess failed (%lu)",
                         (unsigned long)GetLastError());
            free(p);
            return NULL;
        }
        CloseHandle(pi.hThread);
        p->process = pi.hProcess;
    }
#else
    {
        pid_t pid = fork();
        if (pid < 0) {
            if (errmsg)
                snprintf(errmsg, errsz, "fork failed: %s", strerror(errno));
            free(p);
            return NULL;
        }
        if (pid == 0) {
            char *argv[2] = { (char *)path, NULL };
            struct rlimit rl;
            int max_fd;
            int fd;

            /*
             * Close every inherited file descriptor above stderr before
             * execvp so the child doesn't hold the dashboard's X11 socket,
             * font mmaps, or anything else that would (a) leak resources
             * back into the child's address space, (b) keep those handles
             * alive past dashboard exit. The child only needs stdin/out/err.
             */
            if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
                max_fd = (int)rl.rlim_cur;
            else
                max_fd = 1024;
            if (max_fd > 4096)
                max_fd = 4096;
            for (fd = 3; fd < max_fd; fd++)
                (void)close(fd);

            execvp(path, argv);
            _exit(127);
        }
        p->pid = pid;
    }
#endif
    return p;
}

int compat_proc_poll(CompatProc *p, int *out_exit_code)
{
    if (!p)
        return -1;
    if (p->exited) {
        if (out_exit_code)
            *out_exit_code = p->exit_code;
        return 1;
    }

#ifdef _WIN32
    {
        DWORD code = 0;
        DWORD rc = WaitForSingleObject(p->process, 0);

        if (rc == WAIT_TIMEOUT)
            return 0;
        if (rc == WAIT_FAILED)
            return -1;
        if (!GetExitCodeProcess(p->process, &code))
            return -1;
        p->exited = 1;
        p->exit_code = (int)code;
        if (out_exit_code)
            *out_exit_code = p->exit_code;
        return 1;
    }
#else
    {
        int status;
        pid_t r = waitpid(p->pid, &status, WNOHANG);

        if (r == 0)
            return 0;
        if (r < 0) {
            if (errno == ECHILD) {
                /* Already reaped (e.g. by signal handler) — treat as exited. */
                p->exited = 1;
                p->exit_code = -1;
                if (out_exit_code)
                    *out_exit_code = p->exit_code;
                return 1;
            }
            return -1;
        }
        p->exited = 1;
        if (WIFEXITED(status))
            p->exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            p->exit_code = 128 + WTERMSIG(status);
        else
            p->exit_code = -1;
        if (out_exit_code)
            *out_exit_code = p->exit_code;
        return 1;
    }
#endif
}

void compat_proc_free(CompatProc *p)
{
    if (!p)
        return;
#ifdef _WIN32
    if (p->process)
        CloseHandle(p->process);
#endif
    free(p);
}
