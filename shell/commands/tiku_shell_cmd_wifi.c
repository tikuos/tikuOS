/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_wifi.c - "wifi" command implementation.
 *
 * Glue to the tiku_wireless API: power, status, scan and cached results, joins
 * and, with the net kit's WiFi adapter, the IP link.  No driver state lives in
 * shell code.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_wifi.h"
#include <shell/tiku_shell.h>
#include <kernel/timers/tiku_clock.h>
#include <interfaces/wireless/tiku_wireless.h>
#if TIKU_WIRELESS_AP_ENABLE
#include <interfaces/wireless/tiku_wireless_ap.h>
#if TIKU_DRV_WIFI_IPC_ENABLE
#include <tikukits/net/wifi/tiku_kits_net_router.h>
#endif
#endif

#if defined(TIKU_KITS_NET_WIFI_ENABLE)
/* `wifi up` brings the net stack up over the joined radio with DHCP.  It
 * needs a Wi-Fi driver and the net kit's WiFi adapter
 * (TIKU_KITS_NET_WIFI_ENABLE=1). */
#include <kernel/process/tiku_process.h>
#include <tikukits/net/wifi/tiku_kits_net_wifi.h>
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>
#include <tikukits/net/ipv4/tiku_kits_net_udp.h>
#include <tikukits/net/ipv4/tiku_kits_net_dhcp.h>
extern struct tiku_process tiku_kits_net_dhcp_process;
#endif

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Compare two C strings for exact equality (1 if equal, else 0).
 */
static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

/** @brief Print a BSSID or MAC as six colon-separated hex pairs. */
static void put_bssid(const uint8_t bssid[6])
{
    uint8_t k;
    for (k = 0U; k < 6U; ++k) {
        if (k > 0U) tiku_shell_io_putc(':');
        SHELL_PRINTF("%02x", (unsigned)bssid[k]);
    }
}

/*---------------------------------------------------------------------------*/
/* SUB-COMMANDS                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Print the "wifi" command usage summary.
 */
static void wifi_help(void)
{
    SHELL_PRINTF("wifi on | off            Power the radio up or down\n");
    SHELL_PRINTF("wifi status              Driver state + MAC + link\n");
    SHELL_PRINTF("wifi scan                Scan for nearby networks\n");
    SHELL_PRINTF("wifi list                Show cached scan results\n");
    SHELL_PRINTF("wifi connect SSID PSK    Join WPA2-PSK network "
                 "(PSK \"\": an open one)\n");
#if defined(TIKU_KITS_NET_WIFI_ENABLE)
    SHELL_PRINTF("wifi up                  Bring the IP stack up over WiFi (DHCP)\n");
#endif
    SHELL_PRINTF("wifi connect3 SSID PSK   Join WPA3-SAE network\n");
    SHELL_PRINTF("wifi disconnect          Leave the current network\n");
    SHELL_PRINTF("wifi forget              Clear stored credentials "
                 "(disable cold-boot rejoin)\n");
#if TIKU_WIRELESS_AP_ENABLE
    SHELL_PRINTF("wifi ap start SSID PSK [channel]  Start WPA2/open AP\n");
    SHELL_PRINTF("wifi ap start3 SSID PSK [channel] Start SAE/PMF AP (channel 1..11)\n");
    SHELL_PRINTF("wifi ap stop | status    Stop AP or read its state\n");
#endif
#if TIKU_DRV_WIFI_IPC_ENABLE
    SHELL_PRINTF("wifi dual on|off         Select simultaneous station/AP mode while idle\n");
    SHELL_PRINTF("wifi route on|off|status Control AP IPv4 forwarding\n");
    SHELL_PRINTF("wifi save PASSPHRASE     Encrypt active profiles (12..128 bytes)\n");
    SHELL_PRINTF("wifi unlock PASSPHRASE   Unlock saved profiles after reboot\n");
    SHELL_PRINTF("wifi saved               Show saved SSID and storage result\n");
#endif
    SHELL_PRINTF("wifi help                This help\n");
}

/**
 * @brief Map a TIKU_WIRELESS_LINK_* state code to a display string.
 */
static const char *link_str(uint8_t link_state)
{
    switch (link_state) {
    case TIKU_WIRELESS_LINK_IDLE:       return "idle";
    case TIKU_WIRELESS_LINK_CONNECTING: return "connecting";
    case TIKU_WIRELESS_LINK_JOINED:     return "joined";
    case TIKU_WIRELESS_LINK_FAILED:     return "failed";
    default:                            return "unknown";
    }
}

/**
 * @brief Handle "wifi status": print driver state, MAC, and link info.
 *
 * Queries tiku_wireless_status() and reports up/down state, MAC, last
 * scan AP count and duration, scan-busy flag, IRQ count, link state
 * (with joined SSID or failure code), and RSSI when joined.
 */
static void wifi_status(void)
{
    tiku_wireless_status_t st;
    if (tiku_wireless_status(&st) != 0) {
        SHELL_PRINTF("wifi: status query failed\n");
        return;
    }
    SHELL_PRINTF("State:       %s\n", st.up ? "up" : "down");
    SHELL_PRINTF("MAC:         ");
    put_bssid(st.mac);
    tiku_shell_io_putc('\n');
    if (st.last_scan_ticks == 0UL) {
        SHELL_PRINTF("Last scan:   %u AP%s found\n",
                     (unsigned)st.scan_aps_found,
                     st.scan_aps_found == 1U ? "" : "s");
    } else {
        SHELL_PRINTF("Last scan:   %u AP%s in %lu ms\n",
                     (unsigned)st.scan_aps_found,
                     st.scan_aps_found == 1U ? "" : "s",
                     (unsigned long)((st.last_scan_ticks * 1000UL)
                                     / TIKU_CLOCK_SECOND));
    }
    SHELL_PRINTF("Scan busy:   %s\n",
                 st.scan_in_progress ? "yes" : "no");
    SHELL_PRINTF("IRQ count:   %lu\n", (unsigned long)st.irq_count);
    SHELL_PRINTF("Link:        %s", link_str(st.link_state));
    if (st.link_state == TIKU_WIRELESS_LINK_JOINED && st.joined_ssid_len) {
        uint8_t k;
        SHELL_PRINTF(" -> ");
        for (k = 0U; k < st.joined_ssid_len && k < 32U; ++k) {
            char c = (char)st.joined_ssid[k];
            if (c >= 0x20 && c < 0x7F) tiku_shell_io_putc(c);
            else                       tiku_shell_io_putc('.');
        }
    }
    if (st.link_state == TIKU_WIRELESS_LINK_FAILED) {
        SHELL_PRINTF(" (raw=0x%lx)", (unsigned long)st.link_status_raw);
    }
    tiku_shell_io_putc('\n');
    if (st.link_state == TIKU_WIRELESS_LINK_JOINED && st.rssi_dbm != 0) {
        SHELL_PRINTF("RSSI:        %d dBm\n", (int)st.rssi_dbm);
    }
}

/**
 * @brief Handle "wifi connect" and "wifi connect3": queue a join.
 *
 * Asked while joined, the radio leaves the current network for the new one.
 * TIKU_DRV_ERR_TIMEOUT, a radio busy with an earlier request, prints a retry
 * hint; any other refusal prints the causes it can have.
 */
static void wifi_connect(uint8_t argc, const char *argv[],
                         tiku_wireless_auth_t auth)
{
    int rc;
    if (argc < 4U) {
        SHELL_PRINTF("usage: wifi %s <ssid> <psk>\n",
                     (auth == TIKU_WIRELESS_AUTH_WPA3_SAE)
                         ? "connect3" : "connect");
        return;
    }
    rc = tiku_wireless_connect_auth(argv[2], argv[3], auth);
    if (rc == 0) {
        SHELL_PRINTF("wifi: %s join requested (ssid=\"%s\"); "
                     "watch 'wifi status' for result.\n",
                     (auth == TIKU_WIRELESS_AUTH_WPA3_SAE) ? "WPA3-SAE" :
                     argv[3][0] == '\0' ? "open" : "WPA2-PSK",
                     argv[2]);
    } else if (rc == TIKU_DRV_ERR_TIMEOUT) {
        SHELL_PRINTF("wifi: connect rejected (rc=%d) — radio busy with a "
                     "scan, join or disconnect; retry when it ends\n", rc);
    } else {
        SHELL_PRINTF("wifi: connect rejected (rc=%d) — radio not up, "
                     "bad creds, or an auth the radio lacks\n", rc);
    }
}

/**
 * @brief Handle "wifi disconnect": leave the current network.
 *
 * Calls tiku_wireless_disconnect() and reports whether the request was
 * queued or failed.
 */
static void wifi_disconnect(void)
{
    int rc = tiku_wireless_disconnect();
    if (rc == 0) SHELL_PRINTF("wifi: disconnect queued\n");
    else         SHELL_PRINTF("wifi: disconnect failed rc=%d\n", rc);
}

/**
 * @brief Handle "wifi forget": clear stored WiFi credentials.
 *
 * Calls tiku_wireless_forget() so the next cold boot will not auto-rejoin,
 * and reports success or the failure code.
 */
static void wifi_forget(void)
{
    int rc = tiku_wireless_forget();
    if (rc == 0) {
        SHELL_PRINTF("wifi: stored credentials cleared "
                     "(no cold-boot rejoin on next reset)\n");
    } else {
        SHELL_PRINTF("wifi: forget failed rc=%d\n", rc);
    }
}

/**
 * @brief Handle "wifi scan": trigger an active scan for access points.
 *
 * tiku_wireless_scan_start() returns at once and the scan runs in the
 * background; `wifi list` prints the results the driver caches.  Reports
 * rejection if the radio is not up and idle.
 */
static void wifi_scan(void)
{
    int rc = tiku_wireless_scan_start();
    if (rc != 0) {
        SHELL_PRINTF("wifi: scan_start rejected (rc=%d) — "
                     "is the radio up + idle?\n", rc);
        return;
    }
    SHELL_PRINTF("wifi: scan requested. Results print as the runner finds APs;\n"
                 "      run 'wifi list' afterwards to see the cached table.\n");
}

/**
 * @brief Handle "wifi list": print cached results from the last scan.
 *
 * Fetches up to TIKU_WIRELESS_MAX_SCAN_RESULTS entries via
 * tiku_wireless_scan_results() and prints a table of BSSID, RSSI,
 * channel, and SSID; notes when no results are cached.
 */
static void wifi_list(void)
{
    tiku_wireless_ap_t aps[TIKU_WIRELESS_MAX_SCAN_RESULTS];
    uint8_t    n = tiku_wireless_scan_results(aps, TIKU_WIRELESS_MAX_SCAN_RESULTS);
    uint8_t    i, k;

    if (n == 0U) {
        SHELL_PRINTF("wifi: no cached scan results (run 'wifi scan' first)\n");
        return;
    }
    SHELL_PRINTF(" ##  BSSID              RSSI  ch  SSID\n");
    for (i = 0U; i < n; ++i) {
        SHELL_PRINTF(" %2u  ", (unsigned)(i + 1U));
        put_bssid(aps[i].bssid);
        SHELL_PRINTF("  %4d  %2u  ",
                     (int)aps[i].rssi, (unsigned)aps[i].channel);
        for (k = 0U; k < aps[i].ssid_len && k < 32U; ++k) {
            char c = (char)aps[i].ssid[k];
            if (c >= 0x20 && c < 0x7F) tiku_shell_io_putc(c);
            else                       tiku_shell_io_putc('.');
        }
        tiku_shell_io_putc('\n');
    }
}

#if defined(TIKU_KITS_NET_WIFI_ENABLE)
/**
 * @brief Bring the IP stack up over the joined radio.
 *
 * Installs the WiFi link backend in place of SLIP, requests a lease, then
 * starts the DHCP client process; DHCP applies the lease to the IPv4 layer on
 * ACK, so `ip` then shows the acquired address.
 */
static void wifi_up(void)
{
    tiku_wireless_status_t st;
    if (tiku_wireless_status(&st) != 0 ||
        st.link_state != TIKU_WIRELESS_LINK_JOINED) {
        SHELL_PRINTF("wifi: not joined -- run 'wifi connect <ssid> <psk>' first\n");
        return;
    }
    if (tiku_kits_net_wifi_init() != TIKU_KITS_NET_OK) {
        SHELL_PRINTF("wifi: could not install the WiFi link backend\n");
        return;
    }
#if TIKU_WIRELESS_AP_ENABLE
    tiku_wireless_ap_status_t ap;
    if (!tiku_wireless_ap_status(&ap) && ap.active) {
        SHELL_PRINTF("wifi: AP IP service at 192.168.4.1\n");
        return;
    }
#endif
    /* DHCP binds UDP port 68; tiku_kits_net_udp_init() is idempotent. */
    tiku_kits_net_udp_init();
    tiku_kits_net_dhcp_init();
    /* Start the exchange with the station MAC before the DHCP process:
     * the process polls a DISCOVER already in flight, and with none it
     * starts its own exchange with the default MAC.  tiku_process_start()
     * runs the process body at once when the event queue is full, so the
     * exchange is armed first. */
    if (tiku_kits_net_dhcp_start(st.mac) != TIKU_KITS_NET_OK) {
        SHELL_PRINTF("wifi: DHCP start failed (radio up? already bound?)\n");
        return;
    }
    tiku_process_start(&tiku_kits_net_dhcp_process, (tiku_event_data_t)0);
    SHELL_PRINTF("wifi: IP stack now rides WiFi; DHCP requested -- "
                 "run 'ip' shortly for the lease.\n");
}
#endif

/**
 * @brief Power the radio up or down through tiku_wireless_power().
 *
 * Up may take a while: the ESP32-C61 calibrates its RF on the way.  A radio
 * that is always on (the CYW43) refuses down.
 */
static void wifi_power(uint8_t on)
{
    int rc = tiku_wireless_power(on);

    if (rc == TIKU_DRV_OK) {
        SHELL_PRINTF("wifi: radio %s\n", on ? "on" : "off");
    } else {
        SHELL_PRINTF("wifi: power %s failed (%d)\n", on ? "on" : "off", rc);
    }
}

#if TIKU_WIRELESS_AP_ENABLE
/** @brief Start or stop an AP, or print its channel and client count. */
static void wifi_ap(uint8_t argc, const char *argv[])
{
    int r;
    if (argc == 3 && str_eq(argv[2], "status")) {
        tiku_wireless_ap_status_t a;
        if (!tiku_wireless_ap_status(&a))
            SHELL_PRINTF("AP: %s channel=%u clients=%u auth=%s\n", a.active ? "up" : "down",
                         a.channel, a.clients, a.secured == 2 ? "SAE/PMF" : a.secured ? "WPA2" : "open");
        return;
    }
    if (argc == 3 && str_eq(argv[2], "stop")) r = tiku_wireless_ap_stop();
    else if ((argc == 5 || argc == 6) &&
             (str_eq(argv[2], "start") || str_eq(argv[2], "start3"))) {
        unsigned channel = 6;
        if (argc == 6) {
            channel = 0;
            const char *p = argv[5];
            if (!*p) { wifi_help(); return; }
            while (*p) {
                if (*p < '0' || *p > '9' || channel > 11) { wifi_help(); return; }
                channel = channel * 10 + (unsigned)(*p++ - '0');
            }
            if (!channel || channel > 11) { wifi_help(); return; }
        }
        r = tiku_wireless_ap_start_auth(argv[3], argv[4], (uint8_t)channel,
            str_eq(argv[2], "start3") ? TIKU_WIRELESS_AUTH_WPA3_SAE : TIKU_WIRELESS_AUTH_WPA2_PSK);
#if TIKU_DRV_WIFI_IPC_ENABLE && TIKU_KITS_NET_WIFI_ENABLE
        if (!r) r = tiku_kits_net_wifi_init();
#endif
    } else { wifi_help(); return; }
    SHELL_PRINTF("wifi: AP request %s (rc=%d)\n", r ? "failed" : "queued", r);
}
#endif
#if TIKU_DRV_WIFI_IPC_ENABLE
/** @brief Print the saved profile without its passphrase. */
static void wifi_saved(void)
{
    tiku_wireless_saved_profile_t p;
    if (tiku_wireless_saved_profile(&p)) return;
    SHELL_PRINTF("Saved: %s", !p.valid ? "none" : !p.ssid[0] ? "encrypted (locked)" : "encrypted ");
    if (p.valid) for (unsigned i = 0; i < 32 && p.ssid[i]; i++)
        tiku_shell_io_putc(p.ssid[i] >= 32 && p.ssid[i] < 127 ? p.ssid[i] : '.');
    SHELL_PRINTF(" mode=%s channel=%u store_result=%d\n",
                 p.dual ? "dual" : p.access_point ? "ap" : "station", p.channel, p.last_store_result);
}
#endif

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_wifi(uint8_t argc, const char *argv[])
{
    if (argc < 2U) { wifi_help(); return; }
#if TIKU_WIRELESS_AP_ENABLE
    if (str_eq(argv[1], "ap")) { wifi_ap(argc, argv); return; }
#endif
#if TIKU_DRV_WIFI_IPC_ENABLE
    if (str_eq(argv[1], "dual") && argc == 3) {
        int rc = str_eq(argv[2], "on") ? tiku_wireless_dual(1) :
                 str_eq(argv[2], "off") ? tiku_wireless_dual(0) : TIKU_DRV_ERR_INVALID;
        SHELL_PRINTF("wifi: dual request rc=%d\n", rc); return;
    }
#if defined(TIKU_KITS_NET_WIFI_ENABLE)
    if (str_eq(argv[1], "route") && argc == 3) {
        if (str_eq(argv[2], "on")) tiku_kits_net_router_enable(1);
        else if (str_eq(argv[2], "off")) tiku_kits_net_router_enable(0);
        else if (!str_eq(argv[2], "status")) { wifi_help(); return; }
        uint8_t on, up, count; uint32_t drops;
        tiku_kits_net_router_status(&on, &up, &count, &drops);
        SHELL_PRINTF("wifi: route=%u uplink=%u flows=%u drops=%lu\n", on, up, count, (unsigned long)drops);
        return;
    }
#endif
    if ((str_eq(argv[1], "save") || str_eq(argv[1], "unlock")) && argc == 3) {
        int r = str_eq(argv[1], "save") ? tiku_wireless_save_encrypted(argv[2]) :
                tiku_wireless_unlock_profile(argv[2]);
        SHELL_PRINTF("wifi: profile %s (rc=%d)\n", r ? "failed" : "complete", r);
        return;
    }
    if (str_eq(argv[1], "saved") && argc == 2) { wifi_saved(); return; }
#endif

#if defined(TIKU_KITS_NET_WIFI_ENABLE)
    if (str_eq(argv[1], "up")) { wifi_up(); return; }
#endif
    if      (str_eq(argv[1], "on"))         wifi_power(1U);
    else if (str_eq(argv[1], "off"))        wifi_power(0U);
    else if (str_eq(argv[1], "status"))     wifi_status();
    else if (str_eq(argv[1], "scan"))       wifi_scan();
    else if (str_eq(argv[1], "list"))       wifi_list();
    else if (str_eq(argv[1], "connect"))    wifi_connect(argc, argv,
                                                TIKU_WIRELESS_AUTH_WPA2_PSK);
    else if (str_eq(argv[1], "connect3"))   wifi_connect(argc, argv,
                                                TIKU_WIRELESS_AUTH_WPA3_SAE);
    else if (str_eq(argv[1], "disconnect")) wifi_disconnect();
    else if (str_eq(argv[1], "forget"))     wifi_forget();
    else if (str_eq(argv[1], "help"))       wifi_help();
    else {
        SHELL_PRINTF("wifi: unknown subcommand '%s'\n", argv[1]);
        wifi_help();
    }
}
