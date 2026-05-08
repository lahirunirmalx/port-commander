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
 * Reads the `starttime` field (field 22) from /proc/<pid>/stat as
 * jiffies since boot. Stable for the lifetime of the process and
 * unique per-process even across PID reuse, so it's the right
 * fingerprint to capture before kill confirm and re-check before
 * kill — closes the PID-recycling race window.
 *
 * Returns 0 if the PID can't be read or the value can't be parsed.
 */
unsigned long long proc_read_starttime(int pid);

#endif
