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
#  if defined(__linux__)
#    include <sys/syscall.h>
#  endif
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

CompatProc *compat_spawn(const char *path, char *const argv[],
                         char *errmsg, size_t errsz)
{
    CompatProc *p;
    char *const default_argv[2] = { (char *)path, NULL };

    if (errmsg && errsz)
        errmsg[0] = '\0';
    if (!path || !path[0]) {
        if (errmsg)
            snprintf(errmsg, errsz, "compat_spawn: empty path");
        return NULL;
    }
    if (!argv)
        argv = default_argv;
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
        char cmdline[2048];
        char *cp = cmdline;
        char *cend = cmdline + sizeof(cmdline);
        int i;

        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);

        /* Build cmdline with naive but correct enough quoting: each arg
         * containing whitespace is wrapped in double quotes; embedded
         * double quotes are backslash-escaped. */
        for (i = 0; argv[i] && cp < cend - 1; i++) {
            const char *a = argv[i];
            int need_q = (strchr(a, ' ') || strchr(a, '\t'));

            if (i > 0 && cp < cend - 1)
                *cp++ = ' ';
            if (need_q && cp < cend - 1)
                *cp++ = '"';
            while (*a && cp < cend - 1) {
                if (*a == '"' && cp < cend - 2) {
                    *cp++ = '\\';
                    *cp++ = '"';
                } else {
                    *cp++ = *a;
                }
                a++;
            }
            if (need_q && cp < cend - 1)
                *cp++ = '"';
        }
        *cp = '\0';

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
            /*
             * Close every inherited file descriptor above stderr before
             * execvp so the child doesn't hold the dashboard's X11 socket,
             * font mmaps, or anything else that would (a) leak resources
             * back into the child's address space, (b) keep those handles
             * alive past dashboard exit. The child only needs stdin/out/err.
             *
             * Prefer close_range(2) (Linux 5.9+) or closefrom(3) (glibc
             * 2.34+, BSD, Solaris) — both are O(1). Fall back to a loop
             * bounded by RLIMIT_NOFILE; under systemd-style high soft
             * limits we keep iterating past 4096 so FDs opened during a
             * long dashboard session aren't silently leaked into the
             * child. Closing a descriptor we don't own is a defined
             * no-op (EBADF), so a loop that runs "too far" is safe.
             */
            int closed = 0;

#if defined(__linux__) && defined(SYS_close_range)
            if (syscall(SYS_close_range, 3, ~0U, 0) == 0)
                closed = 1;
#endif
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#  if __GLIBC_PREREQ(2, 34)
            if (!closed) {
                closefrom(3);
                closed = 1;
            }
#  endif
#endif
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
    defined(__sun) || defined(__APPLE__)
            if (!closed) {
                closefrom(3);
                closed = 1;
            }
#endif
            if (!closed) {
                struct rlimit rl;
                int max_fd;
                int fd;

                if (getrlimit(RLIMIT_NOFILE, &rl) == 0 &&
                    rl.rlim_cur != RLIM_INFINITY)
                    max_fd = (int)rl.rlim_cur;
                else
                    max_fd = 1024;
                /* No artificial ceiling: a long-running dashboard with a
                 * high soft limit would otherwise leak FDs above 4096. */
                for (fd = 3; fd < max_fd; fd++)
                    (void)close(fd);
            }

            execvp(path, argv);
            _exit(127);
        }
        p->pid = pid;
    }
#endif
    return p;
}

int compat_home_dir(char *out, size_t outsz)
{
    const char *h;

    if (!out || outsz == 0)
        return -1;
    out[0] = '\0';
    h = getenv("HOME");
    if (h && h[0]) {
        snprintf(out, outsz, "%s", h);
        return 0;
    }
#ifdef _WIN32
    h = getenv("USERPROFILE");
    if (h && h[0]) {
        snprintf(out, outsz, "%s", h);
        return 0;
    }
#endif
    return -1;
}

int compat_default_font(char *out, size_t outsz)
{
    /* Common monospace font locations across the platforms we target.
     * Order is: Linux (Debian/Ubuntu/Fedora layout) → macOS bundled
     * fonts → Windows. The first existing/readable file wins. */
    static const char *const candidates[] = {
        /* Linux. */
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf",
        "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation-mono/LiberationMono-Regular.ttf",
        /* macOS. */
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Monaco.ttf",
        "/Library/Fonts/Andale Mono.ttf",
        /* Windows. */
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/lucon.ttf",
        NULL,
    };
    int i;

    if (!out || outsz == 0)
        return -1;
    out[0] = '\0';
    for (i = 0; candidates[i]; i++) {
        FILE *fp = fopen(candidates[i], "rb");
        if (fp) {
            fclose(fp);
            snprintf(out, outsz, "%s", candidates[i]);
            return 0;
        }
    }
    return -1;
}

int compat_path_lookup(const char *name, char *out, size_t outsz)
{
    const char *path_env;
    const char *p;
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif

    if (!name || !name[0] || !out || outsz == 0)
        return -1;
    out[0] = '\0';
    /* Reject names that contain a path separator — caller should resolve
     * those directly (relative to app dir or as-is). */
    if (strchr(name, '/'))
        return -1;
#ifdef _WIN32
    if (strchr(name, '\\'))
        return -1;
#endif
    path_env = getenv("PATH");
    if (!path_env || !path_env[0])
        return -1;

    p = path_env;
    while (*p) {
        const char *end = strchr(p, sep);
        size_t dlen = end ? (size_t)(end - p) : strlen(p);
        char dir[1024];

        if (dlen > 0 && dlen < sizeof(dir)) {
            int written;

            memcpy(dir, p, dlen);
            dir[dlen] = '\0';
            written = snprintf(out, outsz, "%s/%s%s", dir, name,
                               compat_exe_suffix());
            if (written > 0 && (size_t)written < outsz &&
                compat_can_execute(out))
                return 0;
        }
        if (!end)
            break;
        p = end + 1;
    }
    out[0] = '\0';
    return -1;
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
