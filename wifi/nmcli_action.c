#define _GNU_SOURCE
#include "nmcli_action.h"
#include "nmcli_run.h"

#include <stdio.h>
#include <string.h>

int nmcli_hotspot_up(const char *ifname, const char *ssid, const char *password,
                     const char *band, char *err, size_t errsz)
{
    char *argv[16];
    int i = 0;

    if (!ssid || !ssid[0]) {
        if (errsz)
            snprintf(err, errsz, "SSID is required.");
        return -1;
    }
    /* nmcli's outer option parser strips global flags before the subcommand,
     * but defending against a future regression is cheap: reject leading '-'
     * on values that we splice into argv as positional arguments. */
    if (ssid[0] == '-') {
        if (errsz)
            snprintf(err, errsz, "SSID cannot start with '-'.");
        return -1;
    }
    if (!password || strlen(password) < 8) {
        if (errsz)
            snprintf(err, errsz,
                     "Password must be at least 8 characters (WPA requirement).");
        return -1;
    }
    if (password[0] == '-') {
        if (errsz)
            snprintf(err, errsz, "Password cannot start with '-'.");
        return -1;
    }

    argv[i++] = "nmcli";
    argv[i++] = "device";
    argv[i++] = "wifi";
    argv[i++] = "hotspot";
    if (ifname && ifname[0]) {
        argv[i++] = "ifname";
        argv[i++] = (char *)ifname;
    }
    argv[i++] = "ssid";
    argv[i++] = (char *)ssid;
    argv[i++] = "password";
    argv[i++] = (char *)password;
    if (band && (strcmp(band, "bg") == 0 || strcmp(band, "a") == 0)) {
        argv[i++] = "band";
        argv[i++] = (char *)band;
    }
    argv[i] = NULL;

    return nmcli_run_capture_stderr(argv, err, errsz);
}

int nmcli_hotspot_down(const char *conn_name, char *err, size_t errsz)
{
    char *argv[6];

    if (!conn_name || !conn_name[0]) {
        if (errsz)
            snprintf(err, errsz, "Hotspot connection name is unknown.");
        return -1;
    }
    /* conn_name is read from `nmcli connection show --active` so a hostile
     * value would already require local privilege to install — defense in
     * depth all the same. */
    if (conn_name[0] == '-') {
        if (errsz)
            snprintf(err, errsz, "Refusing connection name starting with '-'.");
        return -1;
    }

    argv[0] = "nmcli";
    argv[1] = "connection";
    argv[2] = "down";
    argv[3] = (char *)conn_name;
    argv[4] = NULL;

    return nmcli_run_capture_stderr(argv, err, errsz);
}
