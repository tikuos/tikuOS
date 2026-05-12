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
    SHELL_PRINTF("wifi status         Driver state + MAC\n");
    SHELL_PRINTF("wifi scan           Trigger active scan (results print as found)\n");
    SHELL_PRINTF("wifi list           Show cached results from last scan\n");
    SHELL_PRINTF("wifi help           This help\n");
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

/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_wifi(uint8_t argc, const char *argv[])
{
    if (argc < 2U) { wifi_help(); return; }

    if      (str_eq(argv[1], "status")) wifi_status();
    else if (str_eq(argv[1], "scan"))   wifi_scan();
    else if (str_eq(argv[1], "list"))   wifi_list();
    else if (str_eq(argv[1], "help"))   wifi_help();
    else {
        SHELL_PRINTF("wifi: unknown subcommand '%s'\n", argv[1]);
        wifi_help();
    }
}
