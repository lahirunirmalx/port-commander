#ifndef LSOF_PARSE_H
#define LSOF_PARSE_H

#include <stddef.h>

#define PORT_ROW_COMM_MAX 256
#define PORT_ROW_PROTO_MAX 16
#define PORT_ROW_NAME_MAX 512
#define PORT_ROW_STATE_MAX 32

typedef struct PortRow {
    int pid;
    char comm[PORT_ROW_COMM_MAX];
    char proto[PORT_ROW_PROTO_MAX];
    char name[PORT_ROW_NAME_MAX];
    char state[PORT_ROW_STATE_MAX];
} PortRow;

typedef struct PortTable {
    PortRow *rows;
    size_t count;
    size_t cap;
    char err[512];
} PortTable;

void port_table_init(PortTable *t);
void port_table_free(PortTable *t);

/* Runs: lsof -i -P -n -F. Replaces t->rows. Sets t->err on failure. */
int port_table_refresh(PortTable *t);

#endif
