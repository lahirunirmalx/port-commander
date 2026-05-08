#include "lsof_parse.h"
#include "spawn_util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_CAP_MAX 20000
#define LSOF_LINE_MAX 4096

static void copy_field(char *dst, size_t dstsz, const char *src)
{
    if (dstsz == 0)
        return;
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
}

static int append_row(PortTable *t, const PortRow *row)
{
    if (t->count >= ROW_CAP_MAX) {
        copy_field(t->err, sizeof(t->err),
                   "Row limit reached; output truncated.");
        return -1;
    }
    if (t->count >= t->cap) {
        size_t ncap = t->cap ? t->cap * 2 : 256;
        PortRow *nr = realloc(t->rows, ncap * sizeof(PortRow));
        if (!nr)
            return -1;
        t->rows = nr;
        t->cap = ncap;
    }
    t->rows[t->count++] = *row;
    return 0;
}

static void flush_fd_row(PortTable *t, int pid, const char *comm,
                         const char *proto, const char *name, const char *state)
{
    if (pid <= 0)
        return;
    if (proto[0] == '\0' || name[0] == '\0')
        return;
    if (strcmp(proto, "TCP") != 0 && strcmp(proto, "UDP") != 0)
        return;

    PortRow row;
    memset(&row, 0, sizeof(row));
    row.pid = pid;
    copy_field(row.comm, sizeof(row.comm), comm);
    copy_field(row.proto, sizeof(row.proto), proto);
    copy_field(row.name, sizeof(row.name), name);
    copy_field(row.state, sizeof(row.state), state);
    (void)append_row(t, &row);
}

void port_table_init(PortTable *t)
{
    memset(t, 0, sizeof(*t));
}

void port_table_free(PortTable *t)
{
    free(t->rows);
    port_table_init(t);
}

/*
 * Parses one decimal int from `s` into *out. Returns 0 on a successful
 * parse of a positive value; -1 otherwise. Replaces atoi(), which
 * silently returns 0 on garbage and gives no overflow signal.
 */
static int parse_pos_int(const char *s, int *out)
{
    char *endp;
    long v;

    if (!s || !s[0])
        return -1;
    errno = 0;
    v = strtol(s, &endp, 10);
    if (endp == s || errno == ERANGE || v <= 0 || v > INT_MAX)
        return -1;
    *out = (int)v;
    return 0;
}

int port_table_refresh(PortTable *t)
{
    char lsof_path[1024];
    char *argv[7];
    pid_t pid_child = -1;
    FILE *fp;
    char line[LSOF_LINE_MAX];
    int pid = -1;
    int reached_cap = 0;
    char comm[PORT_ROW_COMM_MAX];
    char proto[PORT_ROW_PROTO_MAX];
    char name[PORT_ROW_NAME_MAX];
    char state[PORT_ROW_STATE_MAX];

    t->err[0] = '\0';
    free(t->rows);
    t->rows = NULL;
    t->count = 0;
    t->cap = 0;

    comm[0] = '\0';
    proto[0] = '\0';
    name[0] = '\0';
    state[0] = '\0';

    if (spawn_resolve_abs("lsof", lsof_path, sizeof(lsof_path)) != 0) {
        copy_field(t->err, sizeof(t->err),
                   "lsof not found in /usr/bin or /usr/sbin.");
        return -1;
    }

    /* Build argv with flag-separated arguments — execv takes them as
     * discrete elements, so no shell parsing happens. The previous
     * popen("lsof -i -P -n -F 2>/dev/null") form invoked /bin/sh and
     * was vulnerable to PATH planting under sudo without secure_path. */
    argv[0] = lsof_path;
    argv[1] = "-i";
    argv[2] = "-P";
    argv[3] = "-n";
    argv[4] = "-F";
    argv[5] = NULL;

    fp = spawn_capture_stdout(lsof_path, argv, &pid_child);
    if (!fp) {
        copy_field(t->err, sizeof(t->err), "spawn(lsof) failed.");
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';
        if (line[0] == '\0')
            continue;

        switch (line[0]) {
        case 'p': {
            int p_int;

            flush_fd_row(t, pid, comm, proto, name, state);
            proto[0] = '\0';
            name[0] = '\0';
            state[0] = '\0';
            if (parse_pos_int(line + 1, &p_int) == 0)
                pid = p_int;
            else
                pid = -1; /* malformed — drop subsequent fields for it */
            break;
        }
        case 'c':
            copy_field(comm, sizeof(comm), line + 1);
            break;
        case 'f': {
            flush_fd_row(t, pid, comm, proto, name, state);
            proto[0] = '\0';
            name[0] = '\0';
            state[0] = '\0';
            break;
        }
        case 'P':
            copy_field(proto, sizeof(proto), line + 1);
            break;
        case 'n':
            copy_field(name, sizeof(name), line + 1);
            break;
        case 'T':
            if (strncmp(line + 1, "ST=", 3) == 0)
                copy_field(state, sizeof(state), line + 1 + 3);
            break;
        default:
            break;
        }

        if (t->err[0] && t->count >= ROW_CAP_MAX) {
            reached_cap = 1;
            break;
        }
    }

    flush_fd_row(t, pid, comm, proto, name, state);

    /* If we broke out of the loop on the row cap, drain the rest of
     * the pipe before fclose so the child isn't blocked in write(2).
     * Without this, spawn_reap below could hang indefinitely on a
     * host with very many sockets. */
    if (reached_cap) {
        while (fgets(line, sizeof(line), fp)) {
            /* discard */
        }
    }

    fclose(fp);
    if (spawn_reap(pid_child) < 0 && t->err[0] == '\0') {
        copy_field(t->err, sizeof(t->err), "lsof exited abnormally.");
        return -1;
    }

    if (pid < 0 && t->count == 0 && t->err[0] == '\0')
        copy_field(t->err, sizeof(t->err),
                   "No sockets (or lsof denied). Try sudo.");

    return 0;
}
