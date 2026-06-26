/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_wifi.c - "wifi" command implementation
 *
 * Subcommands:
 *   wifi status           — driver state, MAC, scan count
 *   wifi scan             — trigger a fresh scan; cached results print
 *                           when SCAN_COMPLETE event arrives
 *   wifi list             — show the cached scan results from last scan
 *   wifi help             — usage
 *
 * Implementation just glues to drivers/wifi/cyw43/whd.h's public
 * API: tiku_wireless_status(), tiku_wireless_scan_start(),
 * tiku_wireless_scan_results(). No driver state lives in shell code.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_wifi.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/timers/tiku_clock.h>
#include <interfaces/wireless/tiku_wireless.h>

#if defined(TIKU_KITS_NET_WIFI_ENABLE)
/* WiFi as the IP link: bring the net stack up over the joined radio (DHCP),
 * instead of SLIP-over-UART. Needs the CYW43 driver + the net kit's WiFi
 * adapter (TIKU_DRV_WIFI_CYW43_ENABLE=1 TIKU_KITS_NET_WIFI_ENABLE=1). */
#include <kernel/process/tiku_process.h>
#include <tikukits/net/wifi/tiku_kits_net_wifi.h>
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>
#include <tikukits/net/ipv4/tiku_kits_net_udp.h>
#include <tikukits/net/ipv4/tiku_kits_net_dhcp.h>
extern struct tiku_process tiku_kits_net_dhcp_process;
#endif

/*---------------------------------------------------------------------------*/

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

/* SHELL_PRINTF doesn't honour %02x's zero-pad — print two hex
 * nibbles explicitly so "00:0f:..." stays aligned. */
static void put_hex2(uint8_t b)
{
    static const char digits[] = "0123456789abcdef";
    tiku_shell_io_putc(digits[(b >> 4) & 0xFU]);
    tiku_shell_io_putc(digits[b & 0xFU]);
}

static void put_bssid(const uint8_t bssid[6])
{
    uint8_t k;
    for (k = 0U; k < 6U; ++k) {
        if (k > 0U) tiku_shell_io_putc(':');
        put_hex2(bssid[k]);
    }
}

/*---------------------------------------------------------------------------*/

static void wifi_help(void)
{
    SHELL_PRINTF("wifi status              Driver state + MAC + link\n");
    SHELL_PRINTF("wifi scan                Trigger active scan\n");
    SHELL_PRINTF("wifi list                Show cached scan results\n");
    SHELL_PRINTF("wifi connect SSID PSK    Join WPA2-PSK network\n");
#if defined(TIKU_KITS_NET_WIFI_ENABLE)
    SHELL_PRINTF("wifi up                  Bring the IP stack up over WiFi (DHCP)\n");
#endif
    SHELL_PRINTF("wifi connect3 SSID PSK   Join WPA3-SAE network\n");
    SHELL_PRINTF("wifi disconnect          Leave the current network\n");
    SHELL_PRINTF("wifi forget              Clear stored credentials "
                 "(disable cold-boot rejoin)\n");
    SHELL_PRINTF("wifi help                This help\n");
}

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
                     (auth == TIKU_WIRELESS_AUTH_WPA3_SAE)
                         ? "WPA3-SAE" : "WPA2-PSK",
                     argv[2]);
    } else {
        SHELL_PRINTF("wifi: connect rejected (rc=%d) — radio not up, "
                     "join already in flight, or bad creds\n", rc);
    }
}

static void wifi_disconnect(void)
{
    int rc = tiku_wireless_disconnect();
    if (rc == 0) SHELL_PRINTF("wifi: disconnect queued\n");
    else         SHELL_PRINTF("wifi: disconnect failed rc=%d\n", rc);
}

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
/* Bring the IP stack up over the joined radio: install the WiFi link backend in
 * place of SLIP, start the DHCP client process, and request a lease (broadcast
 * DORA over WiFi).  The lease binds asynchronously; DHCP applies it to the IPv4
 * layer on ACK, so `ip` then shows the acquired address. */
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
    /* Self-contained: ensure UDP is up (DHCP binds port 68). Harmless if the
     * net-test path already did it; lets a lean net build (no NET_TEST) work. */
    tiku_kits_net_udp_init();
    tiku_kits_net_dhcp_init();
    tiku_process_start(&tiku_kits_net_dhcp_process, (tiku_event_data_t)0);
    if (tiku_kits_net_dhcp_start(st.mac) != TIKU_KITS_NET_OK) {
        SHELL_PRINTF("wifi: DHCP start failed (radio up? already bound?)\n");
        return;
    }
    SHELL_PRINTF("wifi: IP stack now rides WiFi; DHCP requested -- "
                 "run 'ip' shortly for the lease.\n");
}
#endif

/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_wifi(uint8_t argc, const char *argv[])
{
    if (argc < 2U) { wifi_help(); return; }

#if defined(TIKU_KITS_NET_WIFI_ENABLE)
    if (str_eq(argv[1], "up")) { wifi_up(); return; }
#endif
    if      (str_eq(argv[1], "status"))     wifi_status();
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
