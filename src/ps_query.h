#ifndef PS_QUERY_H
#define PS_QUERY_H

#include <stddef.h>

typedef struct ProcessDetail {
    int valid; /* 1 if /proc/<pid> could be read */
    int pid;
    char name[256];
    char uid[32];
    char gid[32];
    char etime[64];
    char cmdline[4096];
    char err[256];
} ProcessDetail;

/*
 * Fills detail from /proc/<pid>/status, cmdline, and ps -o etime=.
 * Returns 0 if something was read, -1 on total failure (see ->err).
 */
int process_detail_load(int pid, ProcessDetail *d);

/*
 * Legacy one-line ps summary (optional).
 */
int ps_query_pid(int pid, char *buf, size_t buflen);

#endif
