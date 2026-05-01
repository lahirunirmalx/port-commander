#define _GNU_SOURCE
#include "nmcli_query.h"
#include "nmcli_run.h"
#include "qr_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define ROW_CAP_MAX 4096
#define LINE_MAX 1024

static void copy_field(char *dst, size_t dstsz, const char *src)
{
    if (dstsz == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
}

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

/*
 * Splits a terse-mode nmcli line in place. nmcli -t separates fields with
 * ':' and escapes literal ':' as '\:' and literal '\' as '\\'. Mutates the
 * line so each separator becomes '\0' and escapes are decoded. Returns the
 * number of fields found.
 */
static int split_terse(char *line, char *fields[], int max_fields)
{
    int n = 0;
    char *src = line;
    char *dst = line;

    if (!line || max_fields < 1)
        return 0;
    fields[n++] = dst;
    while (*src) {
        if (*src == '\\' && (src[1] == ':' || src[1] == '\\')) {
            *dst++ = src[1];
            src += 2;
        } else if (*src == ':') {
            *dst++ = '\0';
            src++;
            if (n < max_fields)
                fields[n++] = dst;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return n;
}

static int append_row(WifiTable *t, const WifiRow *row)
{
    if (t->count >= ROW_CAP_MAX) {
        copy_field(t->err, sizeof(t->err), "Row limit reached.");
        return -1;
    }
    if (t->count >= t->cap) {
        size_t ncap = t->cap ? t->cap * 2 : 64;
        WifiRow *nr = realloc(t->rows, ncap * sizeof(WifiRow));

        if (!nr)
            return -1;
        t->rows = nr;
        t->cap = ncap;
    }
    t->rows[t->count++] = *row;
    return 0;
}

void wifi_table_init(WifiTable *t)
{
    memset(t, 0, sizeof(*t));
}

void wifi_table_free(WifiTable *t)
{
    free(t->rows);
    wifi_table_init(t);
}

int wifi_table_refresh(WifiTable *t, const char *ifname)
{
    char *argv_with_iface[] = {
        "nmcli",  "-t",   "-f",      "IN-USE,SIGNAL,SECURITY,SSID,BSSID",
        "device", "wifi", "list",    "ifname",
        NULL,     NULL };
    char *argv_no_iface[] = {
        "nmcli",  "-t",   "-f",   "IN-USE,SIGNAL,SECURITY,SSID,BSSID",
        "device", "wifi", "list", NULL };
    char *const *argv;
    pid_t pid = -1;
    FILE *fp;
    char line[LINE_MAX];

    t->err[0] = '\0';
    free(t->rows);
    t->rows = NULL;
    t->count = 0;
    t->cap = 0;

    if (ifname && ifname[0]) {
        argv_with_iface[9] = (char *)ifname;
        argv = argv_with_iface;
    } else {
        argv = argv_no_iface;
    }

    fp = nmcli_spawn_stdout(argv, &pid);
    if (!fp) {
        copy_field(t->err, sizeof(t->err),
                   "Failed to run nmcli (is NetworkManager installed?).");
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *fields[5];
        int nf;
        WifiRow row;

        trim_newline(line);
        if (line[0] == '\0')
            continue;

        nf = split_terse(line, fields, 5);
        if (nf < 5)
            continue;

        memset(&row, 0, sizeof(row));
        row.in_use = (fields[0] && fields[0][0] == '*') ? 1 : 0;
        row.signal = fields[1] ? atoi(fields[1]) : 0;
        copy_field(row.security, sizeof(row.security),
                   (fields[2] && fields[2][0]) ? fields[2] : "--");
        copy_field(row.ssid, sizeof(row.ssid),
                   (fields[3] && fields[3][0]) ? fields[3] : "<hidden>");
        copy_field(row.bssid, sizeof(row.bssid), fields[4]);
        (void)append_row(t, &row);
    }
    fclose(fp);

    if (nmcli_reap(pid) != 0 && t->err[0] == '\0' && t->count == 0) {
        copy_field(t->err, sizeof(t->err),
                   "nmcli failed (no Wi-Fi device, or scan denied).");
    }
    return 0;
}

void wifi_state_init(WifiState *s)
{
    memset(s, 0, sizeof(*s));
}

static int read_first_line(char *const argv[], char *out, size_t outsz)
{
    pid_t pid = -1;
    FILE *fp;

    if (outsz == 0 || !out)
        return -1;
    out[0] = '\0';
    fp = nmcli_spawn_stdout(argv, &pid);
    if (!fp)
        return -1;
    if (fgets(out, outsz, fp))
        trim_newline(out);
    while (fgetc(fp) != EOF) { /* drain to avoid SIGPIPE in child */ }
    fclose(fp);
    return nmcli_reap(pid);
}

static int conn_get(const char *name, const char *field, int with_secrets,
                    char *out, size_t outsz)
{
    char *argv_secret[] = { "nmcli",      "-s",          "-g",        (char *)field,
                            "connection", "show",        (char *)name, NULL };
    char *argv_plain[] = { "nmcli",      "-g",          (char *)field,
                           "connection", "show",        (char *)name, NULL };

    return read_first_line(with_secrets ? argv_secret : argv_plain, out, outsz);
}

int wifi_state_refresh(WifiState *s)
{
    pid_t pid = -1;
    FILE *fp;
    char line[LINE_MAX];
    char *argv_devs[] = { "nmcli", "-t", "-f", "DEVICE,TYPE,STATE",
                          "device", NULL };
    char *argv_active[] = { "nmcli",      "-t",   "-f",
                            "NAME,DEVICE,TYPE",
                            "connection", "show", "--active", NULL };
    char active_conn[WIFI_CONN_NAME_MAX];

    wifi_state_init(s);
    active_conn[0] = '\0';

    fp = nmcli_spawn_stdout(argv_devs, &pid);
    if (!fp) {
        copy_field(s->err, sizeof(s->err),
                   "nmcli not available (install network-manager).");
        return -1;
    }
    while (fgets(line, sizeof(line), fp)) {
        char *f[3];
        int n;

        trim_newline(line);
        n = split_terse(line, f, 3);
        if (n >= 2 && strcmp(f[1], "wifi") == 0) {
            copy_field(s->ifname, sizeof(s->ifname), f[0]);
            break;
        }
    }
    while (fgets(line, sizeof(line), fp)) { /* drain */ }
    fclose(fp);
    nmcli_reap(pid);

    if (!s->ifname[0]) {
        copy_field(s->err, sizeof(s->err), "No Wi-Fi device found by nmcli.");
        return -1;
    }

    fp = nmcli_spawn_stdout(argv_active, &pid);
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            char *f[3];
            int n;

            trim_newline(line);
            n = split_terse(line, f, 3);
            if (n >= 3 && strcmp(f[1], s->ifname) == 0 &&
                strcmp(f[2], "802-11-wireless") == 0) {
                copy_field(active_conn, sizeof(active_conn), f[0]);
                break;
            }
        }
        while (fgets(line, sizeof(line), fp)) { /* drain */ }
        fclose(fp);
        nmcli_reap(pid);
    }

    if (!active_conn[0])
        return 0;

    {
        char mode[32];

        if (conn_get(active_conn, "802-11-wireless.mode", 0, mode,
                     sizeof(mode)) == 0 &&
            strcmp(mode, "ap") == 0) {
            s->hotspot_active = 1;
            copy_field(s->hotspot_conn, sizeof(s->hotspot_conn), active_conn);
            (void)conn_get(active_conn, "802-11-wireless.ssid", 0,
                           s->hotspot_ssid, sizeof(s->hotspot_ssid));
            (void)conn_get(active_conn, "802-11-wireless-security.psk", 1,
                           s->hotspot_password, sizeof(s->hotspot_password));
            (void)conn_get(active_conn, "802-11-wireless.band", 0,
                           s->hotspot_band, sizeof(s->hotspot_band));
            /* Best-effort: silent skip if qrencode isn't installed. */
            (void)qr_build_wifi(s->hotspot_ssid, s->hotspot_password,
                                s->qr_text, sizeof(s->qr_text));
        } else {
            (void)conn_get(active_conn, "802-11-wireless.ssid", 0,
                           s->active_ssid, sizeof(s->active_ssid));
        }
    }
    return 0;
}
