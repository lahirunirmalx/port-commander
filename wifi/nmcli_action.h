#ifndef NMCLI_ACTION_H
#define NMCLI_ACTION_H

#include <stddef.h>

/*
 * Brings up a Wi-Fi hotspot via:
 *   nmcli device wifi hotspot ifname <iface> ssid <ssid> password <pwd> [band <band>]
 *
 * SSID and password are passed through execvp argv[], not via a shell, so
 * special characters in either are safe.
 *
 * band: "" / NULL = let nmcli decide; "bg" = 2.4 GHz; "a" = 5 GHz.
 * Returns 0 on success, -1 on failure (err receives nmcli's stderr).
 */
int nmcli_hotspot_up(const char *ifname, const char *ssid, const char *password,
                     const char *band, char *err, size_t errsz);

/*
 * Tears down the named connection via `nmcli connection down <conn_name>`.
 * Returns 0 on success, -1 on failure (err receives nmcli's stderr).
 */
int nmcli_hotspot_down(const char *conn_name, char *err, size_t errsz);

#endif
