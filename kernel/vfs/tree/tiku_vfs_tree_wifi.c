/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_wifi.c - /sys/net/wifi VFS nodes.
 *
 * Status reads are snapshots of the radio's cached state; scan, disconnect,
 * forget and connect are explicit writes.  Two staging profiles live in RAM
 * for the boot, and the radio keeps the last profile that joined, if it can.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"
#include "tiku_vfs_tree_wifi.h"

#if TIKU_VFS_NET_WIFI

#include <interfaces/wireless/tiku_wireless.h>
#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/** Number of RAM staging profiles under /sys/net/wifi/profiles. */
#define WIFI_PROFILE_COUNT 2

/** An SSID rendered with every byte escaped as \xNN, NUL included. */
#define WIFI_SSID_TEXT_MAX (TIKU_WIRELESS_SSID_MAX * 4U + 1U)

/** One staging profile; never written to NVM, its password is write-only. */
typedef struct {
    char    ssid[TIKU_WIRELESS_SSID_MAX + 1U];    /**< NUL-terminated     */
    char    password[TIKU_WIRELESS_PSK_MAX + 1U]; /**< NUL-terminated     */
    uint8_t auth;                                 /**< tiku_wireless_auth_t */
} wifi_profile_t;

static wifi_profile_t profiles[WIFI_PROFILE_COUNT];

static const tiku_vfs_desc_t text_desc = TIKU_VFS_DESC(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t bool_desc = TIKU_VFS_DESC(
    TIKU_VFS_T_BOOL, TIKU_VFS_U_BOOL, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t secret_desc = TIKU_VFS_DESC_FLAGS(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE,
    TIKU_VFS_DF_SECRET);

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Zero a buffer that held a password.
 *
 * Writes through a volatile pointer so the compiler keeps the stores.
 */
static void
wifi_clear(void *ptr, size_t count)
{
    volatile unsigned char *p = ptr;

    while (count--) {
        *p++ = 0;
    }
}

/**
 * @brief Length of a written value without one trailing "\n" or "\r\n".
 */
static size_t
line_length(const char *buf, size_t len)
{
    if (len > 0u && buf[len - 1u] == '\n') {
        len--;
    }
    if (len > 0u && buf[len - 1u] == '\r') {
        len--;
    }
    return len;
}

/**
 * @brief True when a written value is exactly @p expected, line end aside.
 */
static int
token(const char *buf, size_t len, const char *expected)
{
    len = line_length(buf, len);
    return len == strlen(expected) && memcmp(buf, expected, len) == 0;
}

/**
 * @brief Map a tiku_wireless_* result to a VFS status.
 *
 * A bad argument is EINVAL, a busy radio (TIKU_DRV_ERR_TIMEOUT) is EBUSY, a
 * call the radio does not support is ENOTSUP, and any other failure is EIO.
 */
static int
wifi_result(int rc)
{
    if (rc == TIKU_DRV_ERR_INVALID) {
        return TIKU_VFS_EINVAL;
    }
    if (rc == TIKU_DRV_ERR_TIMEOUT) {
        return TIKU_VFS_EBUSY;
    }
    if (rc == TIKU_DRV_ERR_NOT_PRESENT) {
        return TIKU_VFS_ENOTSUP;
    }
    return (rc != TIKU_DRV_OK) ? TIKU_VFS_EIO : TIKU_VFS_OK;
}

/** @brief Take a status snapshot; TIKU_VFS_OK or a VFS status. */
static int
snapshot(tiku_wireless_status_t *s)
{
    return wifi_result(tiku_wireless_status(s));
}

/**
 * @brief Render an SSID from a beacon or a record as one printable line.
 *
 * Bytes outside printable ASCII, and the backslash, become \xNN escapes.
 */
static void
ssid_text(const uint8_t *src, unsigned len, char out[WIFI_SSID_TEXT_MAX])
{
    static const char hex[] = "0123456789abcdef";
    unsigned i;
    unsigned n = 0u;

    if (len > TIKU_WIRELESS_SSID_MAX) {
        len = TIKU_WIRELESS_SSID_MAX;
    }
    for (i = 0u; i < len; i++) {
        uint8_t b = src[i];

        if (b >= 32u && b < 127u && b != '\\') {
            out[n++] = (char)b;
        } else {
            out[n++] = '\\';
            out[n++] = 'x';
            out[n++] = hex[b >> 4];
            out[n++] = hex[b & 15u];
        }
    }
    out[n] = '\0';
}

/*---------------------------------------------------------------------------*/
/* STATUS AND ACTIONS                                                        */
/*---------------------------------------------------------------------------*/

/** @brief /sys/net/wifi/state: down, disconnecting, scanning or the link. */
static int
state_read(char *buf, size_t max)
{
    static const char *const names[] = {
        "idle", "connecting", "joined", "failed"
    };
    tiku_wireless_status_t s;
    const char *state;
    int rc = snapshot(&s);

    if (rc != TIKU_VFS_OK) {
        return rc;
    }
    if (!s.up) {
        state = "down";
    } else if (s.disconnect_pending) {
        state = "disconnecting";
    } else if (s.scan_in_progress) {
        state = "scanning";
    } else if (s.link_state < sizeof names / sizeof names[0]) {
        state = names[s.link_state];
    } else {
        state = "unknown";
    }
    return snprintf(buf, max, "%s\n", state);
}

/** @brief /sys/net/wifi/ssid: the joined SSID, escaped; empty when not. */
static int
ssid_read(char *buf, size_t max)
{
    tiku_wireless_status_t s;
    char escaped[WIFI_SSID_TEXT_MAX];
    int rc = snapshot(&s);

    if (rc != TIKU_VFS_OK) {
        return rc;
    }
    ssid_text(s.joined_ssid,
              (s.link_state == TIKU_WIRELESS_LINK_JOINED) ?
              s.joined_ssid_len : 0u, escaped);
    return snprintf(buf, max, "%s\n", escaped);
}

/** @brief /sys/net/wifi/rssi_dbm: ENOTSUP until joined and polled. */
static int
rssi_read(char *buf, size_t max)
{
    tiku_wireless_status_t s;
    int rc = snapshot(&s);

    if (rc != TIKU_VFS_OK) {
        return rc;
    }
    if (s.link_state != TIKU_WIRELESS_LINK_JOINED || s.rssi_dbm == 0) {
        return TIKU_VFS_ENOTSUP;
    }
    return snprintf(buf, max, "%d\n", s.rssi_dbm);
}

/** @brief /sys/net/wifi/mac: the radio's MAC address. */
static int
mac_read(char *buf, size_t max)
{
    tiku_wireless_status_t s;
    int rc = snapshot(&s);

    if (rc != TIKU_VFS_OK) {
        return rc;
    }
    return snprintf(buf, max, "%02x:%02x:%02x:%02x:%02x:%02x\n",
                    s.mac[0], s.mac[1], s.mac[2], s.mac[3], s.mac[4],
                    s.mac[5]);
}

/** @brief /sys/net/wifi/scan_count: access points the last scan found. */
static int
scan_count_read(char *buf, size_t max)
{
    tiku_wireless_status_t s;
    int rc = snapshot(&s);

    if (rc != TIKU_VFS_OK) {
        return rc;
    }
    return snprintf(buf, max, "%u\n", s.scan_aps_found);
}

/** @brief /sys/net/wifi/scan: "start" begins an active scan. */
static int
scan_write(const char *buf, size_t len)
{
    if (!token(buf, len, "start")) {
        return TIKU_VFS_EINVAL;
    }
    return wifi_result(tiku_wireless_scan_start());
}

/** @brief /sys/net/wifi/disconnect: "disconnect" leaves the network. */
static int
disconnect_write(const char *buf, size_t len)
{
    if (!token(buf, len, "disconnect")) {
        return TIKU_VFS_EINVAL;
    }
    return wifi_result(tiku_wireless_disconnect());
}

/**
 * @brief /sys/net/wifi/saved (write): "forget" leaves the network and
 *        erases any saved profile.
 */
static int
forget_write(const char *buf, size_t len)
{
    if (!token(buf, len, "forget")) {
        return TIKU_VFS_EINVAL;
    }
    return wifi_result(tiku_wireless_forget());
}

/** @brief /sys/net/wifi/saved: the radio's saved profile, without its key. */
static int
saved_read(char *buf, size_t max)
{
    tiku_wireless_saved_profile_t s;
    char escaped[WIFI_SSID_TEXT_MAX];
    int rc = tiku_wireless_saved_profile(&s);

    if (rc != TIKU_DRV_OK) {
        return wifi_result(rc);
    }
    ssid_text((const uint8_t *)s.ssid,
              s.valid ? (unsigned)strlen(s.ssid) : 0u, escaped);
    return snprintf(buf, max, "valid=%u auth=%s last_store_result=%d "
                    "ssid=%s\n", s.valid, s.auth ? "wpa3" : "wpa2",
                    s.last_store_result, escaped);
}

/** @brief /sys/net/wifi/profile_lifetime: what persists and what does not. */
static int
lifetime_read(char *buf, size_t max)
{
    tiku_wireless_saved_profile_t s;
    int keeps = tiku_wireless_saved_profile(&s) != TIKU_DRV_ERR_NOT_PRESENT;

    return snprintf(buf, max, "profiles=ram; saved=%s; "
                    "passwords=write-only\n",
                    keeps ? "last-successful-join" : "none");
}

/*---------------------------------------------------------------------------*/
/* SCAN RESULTS                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Render cached scan result @p index: BSSID, channel, RSSI, SSID.
 *
 * @return Bytes rendered, or TIKU_VFS_ENOENT past the last result
 */
static int
scan_result_read(unsigned index, char *buf, size_t max)
{
    tiku_wireless_ap_t aps[TIKU_WIRELESS_MAX_SCAN_RESULTS];
    char escaped[WIFI_SSID_TEXT_MAX];
    const tiku_wireless_ap_t *ap;
    uint8_t n = tiku_wireless_scan_results(aps,
                                           TIKU_WIRELESS_MAX_SCAN_RESULTS);

    if (index >= n) {
        return TIKU_VFS_ENOENT;
    }
    ap = &aps[index];
    ssid_text(ap->ssid, ap->ssid_len, escaped);
    return snprintf(buf, max, "%02x:%02x:%02x:%02x:%02x:%02x channel=%u "
                    "rssi_dbm=%d ssid=%s\n", ap->bssid[0], ap->bssid[1],
                    ap->bssid[2], ap->bssid[3], ap->bssid[4], ap->bssid[5],
                    ap->channel, ap->rssi, escaped);
}

#define SCAN_WRAPPER(i)                                                      \
    static int scan_##i(char *buf, size_t max)                               \
    {                                                                        \
        return scan_result_read(i, buf, max);                                \
    }

SCAN_WRAPPER(0)
SCAN_WRAPPER(1)
SCAN_WRAPPER(2)
SCAN_WRAPPER(3)
SCAN_WRAPPER(4)
SCAN_WRAPPER(5)
SCAN_WRAPPER(6)
SCAN_WRAPPER(7)
SCAN_WRAPPER(8)
SCAN_WRAPPER(9)
SCAN_WRAPPER(10)
SCAN_WRAPPER(11)
SCAN_WRAPPER(12)
SCAN_WRAPPER(13)
SCAN_WRAPPER(14)
SCAN_WRAPPER(15)

#define SCAN_NODE(i)                                                         \
    { .name = #i, .type = TIKU_VFS_FILE, .read = scan_##i,                   \
      .desc = &text_desc }

static const tiku_vfs_node_t scans[] = {
    SCAN_NODE(0),  SCAN_NODE(1),  SCAN_NODE(2),  SCAN_NODE(3),
    SCAN_NODE(4),  SCAN_NODE(5),  SCAN_NODE(6),  SCAN_NODE(7),
    SCAN_NODE(8),  SCAN_NODE(9),  SCAN_NODE(10), SCAN_NODE(11),
    SCAN_NODE(12), SCAN_NODE(13), SCAN_NODE(14), SCAN_NODE(15),
};

_Static_assert(sizeof(scans) / sizeof(scans[0]) ==
               TIKU_WIRELESS_MAX_SCAN_RESULTS,
               "one /sys/net/wifi/scan_results node per cached result");

/*---------------------------------------------------------------------------*/
/* STAGING PROFILES                                                          */
/*---------------------------------------------------------------------------*/

/** @brief profiles/N/ssid: the staged SSID, escaped. */
static int
profile_ssid_read(unsigned i, char *buf, size_t max)
{
    char escaped[WIFI_SSID_TEXT_MAX];

    ssid_text((const uint8_t *)profiles[i].ssid,
              (unsigned)strlen(profiles[i].ssid), escaped);
    return snprintf(buf, max, "%s\n", escaped);
}

/**
 * @brief profiles/N/ssid: stage an SSID of 1..32 printable bytes.
 *
 * A different SSID clears the staged password; the same one keeps it.
 */
static int
profile_ssid_write(unsigned i, const char *buf, size_t len)
{
    size_t n;

    len = line_length(buf, len);
    if (len == 0u || len > TIKU_WIRELESS_SSID_MAX) {
        return TIKU_VFS_EINVAL;
    }
    for (n = 0u; n < len; n++) {
        if ((unsigned char)buf[n] < 32u || buf[n] == 127) {
            return TIKU_VFS_EINVAL;
        }
    }
    if (strlen(profiles[i].ssid) == len &&
        memcmp(profiles[i].ssid, buf, len) == 0) {
        return TIKU_VFS_OK;
    }
    wifi_clear(profiles[i].password, sizeof profiles[i].password);
    for (n = 0u; n < sizeof profiles[i].ssid; n++) {
        profiles[i].ssid[n] = (n < len) ? buf[n] : '\0';
    }
    return TIKU_VFS_OK;
}

/** @brief profiles/N/auth: "wpa2" or "wpa3". */
static int
profile_auth_read(unsigned i, char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", profiles[i].auth ? "wpa3" : "wpa2");
}

/** @brief profiles/N/auth: a different auth flavor clears the password. */
static int
profile_auth_write(unsigned i, const char *buf, size_t len)
{
    int auth;

    if (token(buf, len, "wpa2")) {
        auth = TIKU_WIRELESS_AUTH_WPA2_PSK;
    } else if (token(buf, len, "wpa3")) {
        auth = TIKU_WIRELESS_AUTH_WPA3_SAE;
    } else {
        return TIKU_VFS_EINVAL;
    }
    if (auth != profiles[i].auth) {
        wifi_clear(profiles[i].password, sizeof profiles[i].password);
    }
    profiles[i].auth = (uint8_t)auth;
    return TIKU_VFS_OK;
}

/**
 * @brief profiles/N/password: stage a printable passphrase for the SSID.
 *
 * WPA2-PSK takes 8..63 bytes, WPA3-SAE 1..63; an SSID must be staged first.
 */
static int
profile_key_write(unsigned i, const char *buf, size_t len)
{
    size_t n;
    size_t min_len = profiles[i].auth ? 1u : 8u;

    len = line_length(buf, len);
    if (profiles[i].ssid[0] == '\0' || len < min_len ||
        len > TIKU_WIRELESS_PSK_MAX) {
        return TIKU_VFS_EINVAL;
    }
    for (n = 0u; n < len; n++) {
        if ((unsigned char)buf[n] < 32u || (unsigned char)buf[n] > 126u) {
            return TIKU_VFS_EINVAL;
        }
    }
    wifi_clear(profiles[i].password, sizeof profiles[i].password);
    for (n = 0u; n < len; n++) {
        profiles[i].password[n] = buf[n];
    }
    return TIKU_VFS_OK;
}

/** @brief profiles/N/ready: 1 once both an SSID and a password are staged. */
static int
profile_ready_read(unsigned i, char *buf, size_t max)
{
    unsigned ready = (profiles[i].ssid[0] != '\0' &&
                      profiles[i].password[0] != '\0') ? 1u : 0u;

    return snprintf(buf, max, "%u\n", ready);
}

/** @brief profiles/N/connect: "connect" joins with the staged profile. */
static int
profile_connect(unsigned i, const char *buf, size_t len)
{
    if (!token(buf, len, "connect") || profiles[i].ssid[0] == '\0' ||
        profiles[i].password[0] == '\0') {
        return TIKU_VFS_EINVAL;
    }
    return wifi_result(tiku_wireless_connect_auth(
        profiles[i].ssid, profiles[i].password,
        (tiku_wireless_auth_t)profiles[i].auth));
}

/** @brief profiles/N/clear: "clear" erases the staged profile. */
static int
profile_clear(unsigned i, const char *buf, size_t len)
{
    if (!token(buf, len, "clear")) {
        return TIKU_VFS_EINVAL;
    }
    wifi_clear(&profiles[i], sizeof profiles[i]);
    return TIKU_VFS_OK;
}

#define PROFILE_WRAPPERS(i)                                                  \
    static int ssid_r_##i(char *buf, size_t max)                             \
    {                                                                        \
        return profile_ssid_read(i, buf, max);                               \
    }                                                                        \
    static int ssid_w_##i(const char *buf, size_t len)                       \
    {                                                                        \
        return profile_ssid_write(i, buf, len);                              \
    }                                                                        \
    static int auth_r_##i(char *buf, size_t max)                             \
    {                                                                        \
        return profile_auth_read(i, buf, max);                               \
    }                                                                        \
    static int auth_w_##i(const char *buf, size_t len)                       \
    {                                                                        \
        return profile_auth_write(i, buf, len);                              \
    }                                                                        \
    static int key_w_##i(const char *buf, size_t len)                        \
    {                                                                        \
        return profile_key_write(i, buf, len);                               \
    }                                                                        \
    static int ready_r_##i(char *buf, size_t max)                            \
    {                                                                        \
        return profile_ready_read(i, buf, max);                              \
    }                                                                        \
    static int connect_w_##i(const char *buf, size_t len)                    \
    {                                                                        \
        return profile_connect(i, buf, len);                                 \
    }                                                                        \
    static int clear_w_##i(const char *buf, size_t len)                      \
    {                                                                        \
        return profile_clear(i, buf, len);                                   \
    }

PROFILE_WRAPPERS(0)
PROFILE_WRAPPERS(1)

#define PROFILE_TABLE(i)                                                     \
    static const tiku_vfs_node_t profile_##i[] = {                           \
        { .name = "ssid", .type = TIKU_VFS_FILE, .read = ssid_r_##i,         \
          .write = ssid_w_##i, .desc = &text_desc,                           \
          .req_cap = TIKU_VFS_CAP_NET },                                     \
        { .name = "auth", .type = TIKU_VFS_FILE, .read = auth_r_##i,         \
          .write = auth_w_##i, .desc = &text_desc,                           \
          .req_cap = TIKU_VFS_CAP_NET },                                     \
        { .name = "password", .type = TIKU_VFS_FILE, .write = key_w_##i,     \
          .desc = &secret_desc, .req_cap = TIKU_VFS_CAP_NET },               \
        { .name = "ready", .type = TIKU_VFS_FILE, .read = ready_r_##i,       \
          .desc = &bool_desc },                                              \
        { .name = "connect", .type = TIKU_VFS_FILE, .write = connect_w_##i,  \
          .req_cap = TIKU_VFS_CAP_NET | TIKU_VFS_CAP_FS },                   \
        { .name = "clear", .type = TIKU_VFS_FILE, .write = clear_w_##i,      \
          .req_cap = TIKU_VFS_CAP_NET },                                     \
    }

PROFILE_TABLE(0);
PROFILE_TABLE(1);

static const tiku_vfs_node_t profile_dirs[] = {
    { .name = "0", .type = TIKU_VFS_DIR, .children = profile_0,
      .child_count = sizeof(profile_0) / sizeof(profile_0[0]) },
    { .name = "1", .type = TIKU_VFS_DIR, .children = profile_1,
      .child_count = sizeof(profile_1) / sizeof(profile_1[0]) },
};

_Static_assert(sizeof(profile_dirs) / sizeof(profile_dirs[0]) ==
               WIFI_PROFILE_COUNT, "one directory per staging profile");

/*---------------------------------------------------------------------------*/
/* /sys/net/wifi                                                             */
/*---------------------------------------------------------------------------*/

const tiku_vfs_node_t tiku_vfs_tree_wifi_children[] = {
    { .name = "state", .type = TIKU_VFS_FILE, .read = state_read,
      .desc = &text_desc },
    { .name = "ssid", .type = TIKU_VFS_FILE, .read = ssid_read,
      .desc = &text_desc },
    { .name = "rssi_dbm", .type = TIKU_VFS_FILE, .read = rssi_read,
      .desc = &text_desc },
    { .name = "mac", .type = TIKU_VFS_FILE, .read = mac_read,
      .desc = &text_desc },
    { .name = "scan_count", .type = TIKU_VFS_FILE, .read = scan_count_read,
      .desc = &text_desc },
    { .name = "scan", .type = TIKU_VFS_FILE, .write = scan_write,
      .req_cap = TIKU_VFS_CAP_NET },
    { .name = "scan_results", .type = TIKU_VFS_DIR, .children = scans,
      .child_count = sizeof(scans) / sizeof(scans[0]) },
    { .name = "disconnect", .type = TIKU_VFS_FILE, .write = disconnect_write,
      .req_cap = TIKU_VFS_CAP_NET },
    { .name = "saved", .type = TIKU_VFS_FILE, .read = saved_read,
      .write = forget_write, .desc = &text_desc,
      .req_cap = TIKU_VFS_CAP_NET | TIKU_VFS_CAP_FS },
    { .name = "profiles", .type = TIKU_VFS_DIR, .children = profile_dirs,
      .child_count = sizeof(profile_dirs) / sizeof(profile_dirs[0]) },
    { .name = "profile_lifetime", .type = TIKU_VFS_FILE,
      .read = lifetime_read, .desc = &text_desc },
};

_Static_assert(sizeof(tiku_vfs_tree_wifi_children) /
               sizeof(tiku_vfs_tree_wifi_children[0])
               == TIKU_VFS_TREE_WIFI_NCHILD,
               "TIKU_VFS_TREE_WIFI_NCHILD out of sync");

#endif /* TIKU_VFS_NET_WIFI */
