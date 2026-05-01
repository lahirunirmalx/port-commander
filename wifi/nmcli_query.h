#ifndef NMCLI_QUERY_H
#define NMCLI_QUERY_H

#include <stddef.h>

#define WIFI_SSID_MAX 64
#define WIFI_PSK_MAX 128 /* 63-char WPA passphrase + NUL with headroom */
#define WIFI_BSSID_MAX 24
#define WIFI_SECURITY_MAX 32
#define WIFI_IFNAME_MAX 32
#define WIFI_CONN_NAME_MAX 128
#define WIFI_BAND_MAX 8
/* qrencode UTF-8 output for a max-payload Wi-Fi QR (~v5, 45 cols × 23 rows
 * × 3 bytes per UTF-8 block char + newlines) fits in ~3.2 KB. 4 KB has
 * headroom for the unlikely edge cases. */
#define WIFI_QR_TEXT_MAX 4096

typedef struct WifiRow {
    int in_use;                       /* 1 if currently connected (lsof '*') */
    int signal;                       /* 0..100 */
    char ssid[WIFI_SSID_MAX];
    char security[WIFI_SECURITY_MAX]; /* WPA2, WPA3, --, etc */
    char bssid[WIFI_BSSID_MAX];
} WifiRow;

typedef struct WifiTable {
    WifiRow *rows;
    size_t count;
    size_t cap;
    char err[512];
} WifiTable;

typedef struct WifiState {
    char ifname[WIFI_IFNAME_MAX];
    char active_ssid[WIFI_SSID_MAX];   /* empty when not connected as client */
    int hotspot_active;                /* 1 when an AP-mode connection is up */
    char hotspot_conn[WIFI_CONN_NAME_MAX];
    char hotspot_ssid[WIFI_SSID_MAX];
    char hotspot_password[WIFI_PSK_MAX];
    char hotspot_band[WIFI_BAND_MAX];  /* "", "bg", or "a" */
    char qr_text[WIFI_QR_TEXT_MAX];    /* qrencode output; empty if disabled */
    char err[512];
} WifiState;

void wifi_table_init(WifiTable *t);
void wifi_table_free(WifiTable *t);

/* Runs `nmcli -t -f IN-USE,SIGNAL,SECURITY,SSID,BSSID device wifi list`
 * (optionally scoped to ifname). Replaces t->rows. Sets t->err on failure. */
int wifi_table_refresh(WifiTable *t, const char *ifname);

void wifi_state_init(WifiState *s);

/* Discovers the first wifi device, the active wireless connection, and
 * (if that connection is in AP mode) the hotspot SSID/password/band. */
int wifi_state_refresh(WifiState *s);

#endif
