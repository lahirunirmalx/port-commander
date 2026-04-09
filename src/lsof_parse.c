#include "lsof_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LSOF_CMD "lsof -i -P -n -F 2>/dev/null"
#define ROW_CAP_MAX 20000
#define LINE_MAX 4096

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

int port_table_refresh(PortTable *t)
{
    FILE *fp;
    char line[LINE_MAX];
    int pid = -1;
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

    fp = popen(LSOF_CMD, "r");
    if (!fp) {
        copy_field(t->err, sizeof(t->err), "popen(lsof) failed.");
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
            flush_fd_row(t, pid, comm, proto, name, state);
            proto[0] = '\0';
            name[0] = '\0';
            state[0] = '\0';
            pid = atoi(line + 1);
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

        if (t->err[0] && t->count >= ROW_CAP_MAX)
            break;
    }

    flush_fd_row(t, pid, comm, proto, name, state);

    if (pclose(fp) == -1) {
        copy_field(t->err, sizeof(t->err), "pclose(lsof) failed.");
        return -1;
    }

    if (pid < 0 && t->count == 0 && t->err[0] == '\0')
        copy_field(t->err, sizeof(t->err),
                   "No sockets (or lsof denied). Try sudo.");

    return 0;
}
