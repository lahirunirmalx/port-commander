#include "ps_query.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim_newline(char *s)
{
    size_t n;

    if (!s)
        return;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void copy_after_tab(const char *line, char *out, size_t outsz)
{
    const char *p = strchr(line, '\t');

    out[0] = '\0';
    if (!p)
        p = strchr(line, ':');
    if (!p)
        return;
    p++;
    while (*p == ' ' || *p == '\t')
        p++;
    strncpy(out, p, outsz - 1);
    out[outsz - 1] = '\0';
    trim_newline(out);
}

static int load_status(int pid, ProcessDetail *d)
{
    char path[64];
    FILE *fp;
    char line[512];

    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    fp = fopen(path, "r");
    if (!fp) {
        snprintf(d->err, sizeof(d->err),
                 "Cannot open /proc/%d/status (gone or denied)", pid);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Name:", 5) == 0)
            copy_after_tab(line, d->name, sizeof(d->name));
        else if (strncmp(line, "Uid:", 4) == 0) {
            char tmp[256];
            unsigned u0, u1, u2, u3;

            copy_after_tab(line, tmp, sizeof(tmp));
            if (sscanf(tmp, "%u %u %u %u", &u0, &u1, &u2, &u3) >= 1)
                snprintf(d->uid, sizeof(d->uid), "%u", u0);
        } else if (strncmp(line, "Gid:", 4) == 0) {
            char tmp[256];
            unsigned g0, g1, g2, g3;

            copy_after_tab(line, tmp, sizeof(tmp));
            if (sscanf(tmp, "%u %u %u %u", &g0, &g1, &g2, &g3) >= 1)
                snprintf(d->gid, sizeof(d->gid), "%u", g0);
        }
    }
    fclose(fp);
    return 0;
}

static int load_cmdline(int pid, ProcessDetail *d)
{
    char path[64];
    FILE *fp;
    char raw[4096];
    size_t n, i, off;

    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    fp = fopen(path, "r");
    if (!fp)
        return -1;

    n = fread(raw, 1, sizeof(raw) - 1, fp);
    fclose(fp);
    raw[n] = '\0';

    for (i = 0, off = 0; i < n && off < sizeof(d->cmdline) - 1; i++) {
        if (raw[i] == '\0') {
            if (off > 0 && d->cmdline[off - 1] != ' ')
                d->cmdline[off++] = ' ';
        } else {
            d->cmdline[off++] = raw[i];
        }
    }
    d->cmdline[off] = '\0';
    while (off > 0 && d->cmdline[off - 1] == ' ') {
        d->cmdline[off - 1] = '\0';
        off--;
    }
    if (d->cmdline[0] == '\0') {
        strncpy(d->cmdline, "(empty or kernel thread)", sizeof(d->cmdline) - 1);
        d->cmdline[sizeof(d->cmdline) - 1] = '\0';
    }
    return 0;
}

static void load_etime(int pid, ProcessDetail *d)
{
    char cmd[128];
    FILE *fp;

    d->etime[0] = '\0';
    if (snprintf(cmd, sizeof(cmd), "ps -p %d -o etime= 2>/dev/null", pid) >=
        (int)sizeof(cmd))
        return;

    fp = popen(cmd, "r");
    if (!fp)
        return;
    if (fgets(d->etime, sizeof(d->etime), fp)) {
        trim_newline(d->etime);
    }
    (void)pclose(fp);
}

int process_detail_load(int pid, ProcessDetail *d)
{
    memset(d, 0, sizeof(*d));
    d->pid = pid;

    if (pid <= 0) {
        strncpy(d->err, "Invalid PID", sizeof(d->err) - 1);
        return -1;
    }

    if (load_status(pid, d) != 0)
        return -1;

    (void)load_cmdline(pid, d);
    load_etime(pid, d);
    d->valid = 1;
    return 0;
}

int ps_query_pid(int pid, char *buf, size_t buflen)
{
    char cmd[256];
    FILE *fp;
    size_t off = 0;

    if (!buf || buflen < 32)
        return -1;
    buf[0] = '\0';

    if (pid <= 0) {
        strncpy(buf, "(no process)", buflen - 1);
        buf[buflen - 1] = '\0';
        return 0;
    }

    if (snprintf(cmd, sizeof(cmd),
                 "ps -p %d -o pid=,uid=,gid=,comm=,etime=,cmd= 2>/dev/null",
                 pid) >= (int)sizeof(cmd))
        return -1;

    fp = popen(cmd, "r");
    if (!fp) {
        strncpy(buf, "ps: popen failed", buflen - 1);
        buf[buflen - 1] = '\0';
        return -1;
    }

    {
        char line[1024];

        while (fgets(line, sizeof(line), fp) && off + 1 < buflen) {
            size_t l = strlen(line);
            if (off + l >= buflen)
                l = buflen - 1 - off;
            memcpy(buf + off, line, l);
            off += l;
            buf[off] = '\0';
        }
    }

    if (pclose(fp) == -1) {
        strncpy(buf, "ps: pclose failed", buflen - 1);
        buf[buflen - 1] = '\0';
        return -1;
    }

    if (off == 0) {
        strncpy(buf, "(ps: no such pid or denied)", buflen - 1);
        buf[buflen - 1] = '\0';
    }

    return 0;
}
