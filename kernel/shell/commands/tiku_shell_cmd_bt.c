/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_bt.c - "bt" command implementation.
 *
 * Calls the public Bluetooth API (tiku_bt.h), whatever controller is under
 * it, and its facades: the shell over the serial pipe and the beacon.  An
 * ESP32-C61 build also shows its driver's heap; an EM9305 build has a probe.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_bt.h"
#include <kernel/shell/tiku_shell.h>
#include <interfaces/bluetooth/tiku_bt.h>
#include <interfaces/bluetooth/tiku_ble_serial.h>  /* bt uart: the NUS pipe */
#include <interfaces/bluetooth/tiku_ble_adv.h>     /* bt beacon            */
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>  /* key print */
#if TIKU_DRV_BLE_ESP_ENABLE
#include <drivers/wifi/esp/tiku_drv_ble_esp.h>
#endif

/* `bt uart` where the build links the serial facade for it (TIKU_BT_UART). */
#define BT_UART (TIKU_BLE_SERIAL_PRESENT && (TIKU_BT_UART + 0))

#if BT_UART
#include <kernel/shell/tiku_shell_io.h>      /* backend swap, rx_ready/getc */
#include <kernel/shell/tiku_shell_parser.h>  /* tiku_shell_parser_execute */
#include <kernel/shell/tiku_shell_cwd.h>     /* the prompt's directory */
#include <kernel/vfs/tiku_vfs.h>             /* TIKU_VFS_CAP_NONE */
#include <kernel/timers/tiku_clock.h>        /* the heartbeat */
#include <hal/tiku_cpu.h>                    /* tiku_cpu_idle_hook */
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
#include <kernel/process/tiku_process.h>     /* the others' events */
#include <kernel/threads/tiku_thread.h>      /* the CPU to the workers */
#endif
#endif
#if TIKU_DRV_BLE_EM9305_ENABLE
#include <arch/ambiq/tiku_em9305.h>          /* bt probe */
#endif

/*---------------------------------------------------------------------------*/

/**
 * @brief Compare two C strings for exact equality (1 if equal, else 0).
 */
static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

/**
 * @brief Print a byte as two zero-padded hex digits.
 */
static void put_hex2(uint8_t b)
{
    static const char digits[] = "0123456789abcdef";
    tiku_shell_io_putc(digits[(b >> 4) & 0xFU]);
    tiku_shell_io_putc(digits[b & 0xFU]);
}

/**
 * @brief Print a 16-bit value as four zero-padded hex nibbles.
 */
static void put_hex4(uint16_t w)
{
    put_hex2((uint8_t)((w >> 8) & 0xFFU));
    put_hex2((uint8_t)(w & 0xFFU));
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Print the "bt" command usage summary.
 */
static void bt_help(void)
{
#if (TIKU_BT_ON_DEMAND + 0)
    SHELL_PRINTF("bt on | off             Power the BLE controller up / down\n");
#endif
    SHELL_PRINTF("bt status               The radio: address + version\n");
    SHELL_PRINTF("bt advertise <name>     Start GAP advertising with local name\n");
    SHELL_PRINTF("bt advertise stop       Stop GAP advertising\n");
    SHELL_PRINTF("bt scan [prefix]        Start LE scan (clears cache; only "
                 "names starting prefix)\n");
    SHELL_PRINTF("bt scan stop            Stop LE scan\n");
    SHELL_PRINTF("bt list                 Print cached scan results\n");
    SHELL_PRINTF("bt connections          List active LE links\n");
    SHELL_PRINTF("bt disconnect [N]       Tear down link N (default: first)\n");
    SHELL_PRINTF("bt connect <slot|addr>  Initiate central-role connection\n");
    SHELL_PRINTF("bt discover [N]         Service discovery on link N\n");
    SHELL_PRINTF("bt read <handle> [N]    ATT Read on link N\n");
    SHELL_PRINTF("bt write <h> <text> [N] ATT Write of text on link N\n");
    SHELL_PRINTF("bt subscribe <h> [N]    Write 0x0001 to CCCD on link N\n");
    SHELL_PRINTF("bt pair [N]             Pair + bond on link N (LE Secure Conn)\n");
    SHELL_PRINTF("bt bonds                List stored LE-SC bonds (LTK slots)\n");
    SHELL_PRINTF("bt unpair <N>|all       Clear bond slot N, or every slot\n");
#if BT_UART
    SHELL_PRINTF("bt uart [name]          The shell over BLE (Nordic UART "
                 "Service) until Ctrl-C\n");
#endif
#if TIKU_BLE_ADV_PRESENT
    SHELL_PRINTF("bt beacon <name> [ms]   Non-connectable beacon (every ms, "
                 "default 1000)\n");
    SHELL_PRINTF("bt beacon off           Stop the beacon\n");
#endif
#if TIKU_DRV_BLE_EM9305_ENABLE
    SHELL_PRINTF("bt probe                EM9305 first contact: SPI and HCI "
                 "Reset (radio off)\n");
#endif
    SHELL_PRINTF("bt help                 this help\n");
}

/**
 * @brief Map an HCI/LMP version code to its Bluetooth Core Spec name.
 *
 * Values per the Assigned Numbers "HCI_Version" table.
 */
static const char *hci_version_name(uint8_t v)
{
    switch (v) {
    case 0x00: return "1.0b";
    case 0x01: return "1.1";
    case 0x02: return "1.2";
    case 0x03: return "2.0+EDR";
    case 0x04: return "2.1+EDR";
    case 0x05: return "3.0+HS";
    case 0x06: return "4.0";
    case 0x07: return "4.1";
    case 0x08: return "4.2";
    case 0x09: return "5.0";
    case 0x0A: return "5.1";
    case 0x0B: return "5.2";
    case 0x0C: return "5.3";
    case 0x0D: return "5.4";
    case 0x0E: return "6.0";
    case 0x0F: return "6.1";
    default:   return "?";
    }
}

/**
 * @brief Print n bytes, rendering non-printable characters as '.'.
 *
 * @param s  Character buffer (not necessarily NUL-terminated).
 * @param n  Number of bytes to render.
 */
static void put_name(const char *s, uint8_t n)
{
    uint8_t i;
    for (i = 0U; i < n; ++i) {
        char c = s[i];
        /* Render only printable ASCII; replace anything else with '.'
         * so a corrupted name can't garble the terminal. */
        if (c >= 0x20 && c < 0x7F) tiku_shell_io_putc(c);
        else                       tiku_shell_io_putc('.');
    }
}

/**
 * @brief Handle "bt status": print what the radio under the stack reports.
 *
 * When the controller is ready, prints the BD_ADDR, HCI and LMP versions
 * (with Core Spec names), manufacturer, firmware string, and current
 * advertising/scanning state.
 */
static void bt_status(void)
{
#if TIKU_DRV_BLE_ESP_ENABLE
    tiku_drv_ble_esp_status_t es;

    tiku_drv_ble_esp_status(&es);
    if (!es.up) {
        SHELL_PRINTF("BT: off (bt on brings it up; library %s)\n",
                     es.version);
        return;
    }
    SHELL_PRINTF("Heap:     %lu of %lu bytes in use, %lu at most, %lu "
                 "refused\n", (unsigned long)es.heap_used,
                 (unsigned long)es.heap_size, (unsigned long)es.heap_peak,
                 (unsigned long)es.heap_refused);
    SHELL_PRINTF("IRQs:     %lu\n", (unsigned long)es.irqs);
    if (es.rx_dropped != 0U) {
        SHELL_PRINTF("Dropped:  %lu HCI packets\n",
                     (unsigned long)es.rx_dropped);
    }
#endif
    if (tiku_bt_is_ready() == 0) {
#if (TIKU_BT_ON_DEMAND + 0)
        SHELL_PRINTF("BT: off (bt on brings it up)\n");
#else
        SHELL_PRINTF("BT: not ready (bring-up failed or not built in)\n");
#endif
        return;
    }

    {
        uint8_t mac[6];
        tiku_bt_version_t v;
        int rc;

        rc = tiku_bt_addr(mac);
        if (rc == 0) {
            uint8_t k;
            SHELL_PRINTF("BD_ADDR:  ");
            for (k = 0U; k < 6U; ++k) {
                if (k > 0U) tiku_shell_io_putc(':');
                put_hex2(mac[k]);
            }
            tiku_shell_io_putc('\n');
        } else {
            SHELL_PRINTF("BD_ADDR:  not cached\n");
        }

        rc = tiku_bt_local_version(&v);
        if (rc == 0) {
            SHELL_PRINTF("HCI:      %u (Core %s)\n",
                         v.hci_version, hci_version_name(v.hci_version));
            SHELL_PRINTF("HCI rev:  0x");
            put_hex4(v.hci_revision); tiku_shell_io_putc('\n');
            SHELL_PRINTF("LMP:      %u (Core %s)\n",
                         v.lmp_version, hci_version_name(v.lmp_version));
            SHELL_PRINTF("LMP sub:  0x");
            put_hex4(v.lmp_subversion); tiku_shell_io_putc('\n');
            SHELL_PRINTF("Mfr:      0x");
            put_hex4(v.manufacturer);
            if (v.manufacturer == 0x000FU)
                SHELL_PRINTF(" (Broadcom)");
            else if (v.manufacturer == 0x0131U)
                SHELL_PRINTF(" (Cypress/Infineon)");
            else if (v.manufacturer == 0x02E5U)
                SHELL_PRINTF(" (Espressif)");
            else if (v.manufacturer == 0x005AU)
                SHELL_PRINTF(" (EM Microelectronic)");
            tiku_shell_io_putc('\n');
        }
        SHELL_PRINTF("BTFW:     %s\n", tiku_bt_fw_version());
        SHELL_PRINTF("Adv:      %s\n",
                     tiku_bt_is_advertising() ? "yes" : "no");
        SHELL_PRINTF("Scan:     %s (%u devices)\n",
                     tiku_bt_is_scanning() ? "yes" : "no",
                     tiku_bt_scan_count());
    }
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Handle "bt advertise <name>" and "bt advertise stop".
 */
static void bt_advertise(uint8_t argc, const char *argv[])
{
    int rc;
    if (argc < 3U) {
        SHELL_PRINTF("usage: bt advertise <name>  |  bt advertise stop\n");
        return;
    }
    if (str_eq(argv[2], "stop")) {
        rc = tiku_bt_advertise_stop();
        SHELL_PRINTF("bt: advertising stopped (rc=%d)\n", rc);
        return;
    }
    rc = tiku_bt_advertise_start(argv[2]);
    if (rc == 0) {
        SHELL_PRINTF("bt: advertising started as \"%s\"\n", argv[2]);
    } else {
        SHELL_PRINTF("bt: advertise start FAILED rc=%d\n", rc);
    }
}

/**
 * @brief Handle "bt scan [prefix]" (start an active scan, caching only the
 *        names that start with @p prefix) and "bt scan stop".
 */
static void bt_scan(uint8_t argc, const char *argv[])
{
    int rc;
    if (argc >= 3U && str_eq(argv[2], "stop")) {
        rc = tiku_bt_scan_stop();
        SHELL_PRINTF("bt: scan stopped (rc=%d, %u devices)\n",
                     rc, tiku_bt_scan_count());
        return;
    }
    tiku_bt_scan_filter(argc >= 3U ? argv[2] : (const char *)0);
    /* Active scan, 100 ms interval, 50 ms window (50% duty). */
    rc = tiku_bt_scan_start(1U, 100U, 50U);
    if (rc != 0) {
        SHELL_PRINTF("bt: scan start FAILED rc=%d\n", rc);
    } else if (argc >= 3U) {
        SHELL_PRINTF("bt: scan started (active, 100/50 ms, names starting "
                     "\"%s\")\n", argv[2]);
    } else {
        SHELL_PRINTF("bt: scan started (active, 100/50 ms)\n");
    }
}

/**
 * @brief Map an LE advertising event-type code to a short label.
 */
static const char *evt_type_name(uint8_t e)
{
    switch (e) {
    case 0x00: return "ADV_IND";
    case 0x01: return "ADV_DIRECT_IND";
    case 0x02: return "ADV_SCAN_IND";
    case 0x03: return "ADV_NONCONN_IND";
    case 0x04: return "SCAN_RSP";
    default:   return "?";
    }
}

/**
 * @brief Map a BLE address-type code to "public"/"random"/"?".
 */
static const char *bt_addr_type_name(uint8_t t)
{
    switch (t) {
    case 0x00: return "public";
    case 0x01: return "random";
    default:   return "?";
    }
}

/**
 * @brief Handle "bt connections": list active LE links in a table.
 *
 * Fetches up to TIKU_BT_CONN_MAX connections via tiku_bt_connections()
 * and prints peer address, address type, role, connection handle, and
 * connection interval for each; notes when none are active.
 */
static void bt_connections(void)
{
    tiku_bt_connection_t conns[TIKU_BT_CONN_MAX];
    uint8_t n = tiku_bt_connections(conns,
                                          TIKU_BT_CONN_MAX);
    uint8_t i;
    if (n == 0U) {
        SHELL_PRINTF("(no active connections)\n");
        return;
    }
    SHELL_PRINTF(" ##  Peer               Type    Role        "
                 "Handle  Interval\n");
    for (i = 0U; i < n; ++i) {
        uint32_t interval_us =
            (uint32_t)conns[i].conn_interval_units * 1250UL;
        SHELL_PRINTF("%3u  ", i + 1U);
        {
            uint8_t k;
            for (k = 0U; k < 6U; ++k) {
                if (k > 0U) tiku_shell_io_putc(':');
                put_hex2(conns[i].peer_addr[k]);
            }
        }
        SHELL_PRINTF(" %s  ", bt_addr_type_name(conns[i].peer_addr_type));
        SHELL_PRINTF("%s   0x",
                     conns[i].role == 1U ? "peripheral" : "central   ");
        put_hex4(conns[i].handle);
        SHELL_PRINTF("  %lu.%lu ms\n",
                     (unsigned long)(interval_us / 1000UL),
                     (unsigned long)((interval_us % 1000UL) / 100UL));
    }
}

/* ---- GATT client helpers ------------------------------------------------- */

/**
 * @brief Parse "aa:bb:cc:dd:ee:ff" into 6 MSB-first bytes.
 * @return 0 on success, -1 on a malformed address
 */
static int parse_mac(const char *s, uint8_t out[6])
{
    uint8_t i;
    for (i = 0U; i < 6U; ++i) {
        uint8_t hi, lo;
        char c1 = *s++;
        char c2 = *s++;
        if (c1 >= '0' && c1 <= '9')      hi = (uint8_t)(c1 - '0');
        else if (c1 >= 'a' && c1 <= 'f') hi = (uint8_t)(10 + c1 - 'a');
        else if (c1 >= 'A' && c1 <= 'F') hi = (uint8_t)(10 + c1 - 'A');
        else return -1;
        if (c2 >= '0' && c2 <= '9')      lo = (uint8_t)(c2 - '0');
        else if (c2 >= 'a' && c2 <= 'f') lo = (uint8_t)(10 + c2 - 'a');
        else if (c2 >= 'A' && c2 <= 'F') lo = (uint8_t)(10 + c2 - 'A');
        else return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
        if (i < 5U) {
            if (*s != ':') return -1;
            ++s;
        }
    }
    return 0;
}

/**
 * @brief Parse 0x-prefixed hex or decimal @p s, at most 0xFFFF, into *out.
 * @return 0 on success, else -1
 */
static int parse_u16(const char *s, uint16_t *out)
{
    uint32_t v = 0UL;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    if (*s == '\0') return -1;
    while (*s) {
        uint32_t d;
        if (*s >= '0' && *s <= '9')      d = (uint32_t)(*s - '0');
        else if (base == 16 && *s >= 'a' && *s <= 'f') d = 10U + (uint32_t)(*s - 'a');
        else if (base == 16 && *s >= 'A' && *s <= 'F') d = 10U + (uint32_t)(*s - 'A');
        else return -1;
        v = v * (uint32_t)base + d;
        if (v > 0xFFFFUL) return -1;
        ++s;
    }
    *out = (uint16_t)v;
    return 0;
}

/** @brief Handle of connection @p slot_1based (1-based), or 0xFFFF. */
static uint16_t conn_handle_for_slot(uint8_t slot_1based)
{
    tiku_bt_connection_t conns[TIKU_BT_CONN_MAX];
    uint8_t n = tiku_bt_connections(conns, TIKU_BT_CONN_MAX);
    if (slot_1based == 0U || slot_1based > n) return 0xFFFFU;
    return conns[slot_1based - 1U].handle;
}

/**
 * @brief Handle of the 1-based link slot in argv[@p pos], or of the first
 *        link when that argument is absent.
 *
 * @return The handle, or 0xFFFF when there is no such link; an unparsable
 *         slot also prints a message
 */
static uint16_t pick_conn_handle(uint8_t argc, const char *argv[], uint8_t pos)
{
    if (argc <= pos) {
        return conn_handle_for_slot(1U);     /* default: first */
    }
    {
        uint16_t n_arg;
        if (parse_u16(argv[pos], &n_arg) != 0 || n_arg > 0xFFU) {
            SHELL_PRINTF("bt: bad slot '%s'\n", argv[pos]);
            return 0xFFFFU;
        }
        return conn_handle_for_slot((uint8_t)n_arg);
    }
}

/**
 * @brief Handle "bt connect <slot|addr> [public]": connect as central.
 */
static void bt_connect_cmd(uint8_t argc, const char *argv[])
{
    uint8_t addr[6];
    uint8_t addr_type = 0U;
    int     rc;

    if (argc < 3U) {
        SHELL_PRINTF("usage: bt connect <slot|aa:bb:cc:dd:ee:ff>\n");
        return;
    }
    /* If it looks like a MAC, parse direct; else treat as scan slot. */
    if (parse_mac(argv[2], addr) == 0) {
        /* A BD_ADDR is taken as a random address unless "public" follows
         * it. */
        addr_type = 1U;
        if (argc >= 4U && str_eq(argv[3], "public")) addr_type = 0U;
    } else {
        uint16_t slot;
        tiku_bt_scan_entry_t entries[TIKU_BT_SCAN_MAX];
        uint8_t n_entries;
        if (parse_u16(argv[2], &slot) != 0 || slot == 0U) {
            SHELL_PRINTF("bt: '%s' is neither a slot number nor a "
                         "BD_ADDR\n", argv[2]);
            return;
        }
        n_entries = tiku_bt_scan_results(entries,
                                               TIKU_BT_SCAN_MAX);
        if (slot > (uint16_t)n_entries) {
            SHELL_PRINTF("bt: no scan entry %u (have %u; run 'bt scan' "
                         "first)\n", slot, n_entries);
            return;
        }
        {
            uint8_t k;
            for (k = 0U; k < 6U; ++k) addr[k] = entries[slot - 1U].addr[k];
            addr_type = entries[slot - 1U].addr_type;
        }
    }
    rc = tiku_bt_connect_to(addr, addr_type);
    if (rc == 0) {
        SHELL_PRINTF("bt: connect requested (wait for "
                     "'*** connected ***' log)\n");
    } else {
        SHELL_PRINTF("bt: connect FAILED rc=%d\n", rc);
    }
}

/**
 * @brief Handle "bt pair [N]": pair and bond on link N (default: first).
 */
static void bt_pair_cmd(uint8_t argc, const char *argv[])
{
    uint16_t h = pick_conn_handle(argc, argv, 2U);
    int rc;
    if (h == 0xFFFFU) {
        SHELL_PRINTF("bt: no connection (run 'bt connect ...' first)\n");
        return;
    }
    rc = tiku_bt_pair(h);
    SHELL_PRINTF(rc == 0 ? "bt: pairing on handle 0x%04x (watch for "
                           "'link encrypted')\n"
                         : "bt: pairing on handle 0x%04x FAILED rc=%d\n",
                 h, rc);
}

/**
 * @brief Handle "bt discover [N]": start service discovery on link N.
 */
static void bt_discover_cmd(uint8_t argc, const char *argv[])
{
    uint16_t h = pick_conn_handle(argc, argv, 2U);
    int rc;
    if (h == 0xFFFFU) {
        SHELL_PRINTF("bt: no connection (run 'bt connect ...' first)\n");
        return;
    }
    rc = tiku_bt_client_discover_services(h);
    SHELL_PRINTF("bt: discover requested on handle 0x%04x (rc=%d)\n",
                 h, rc);
}

/**
 * @brief Handle "bt read <handle> [N]": issue an ATT Read on link N.
 */
static void bt_read_cmd(uint8_t argc, const char *argv[])
{
    uint16_t attr_handle;
    uint16_t conn_handle;
    int      rc;
    if (argc < 3U) {
        SHELL_PRINTF("usage: bt read <handle> [N]\n");
        return;
    }
    if (parse_u16(argv[2], &attr_handle) != 0) {
        SHELL_PRINTF("bt: bad handle '%s'\n", argv[2]);
        return;
    }
    conn_handle = pick_conn_handle(argc, argv, 3U);
    if (conn_handle == 0xFFFFU) {
        SHELL_PRINTF("bt: no connection\n");
        return;
    }
    rc = tiku_bt_client_read(conn_handle, attr_handle);
    SHELL_PRINTF("bt: read requested (rc=%d)\n", rc);
}

/**
 * @brief Handle "bt write <handle> <text> [N]": ATT Write of @p text's
 *        bytes to an attribute on link N; \r, \n and \\ in the text are
 *        a CR, an LF and a backslash.
 */
static void bt_write_cmd(uint8_t argc, const char *argv[])
{
    uint8_t     buf[64];
    const char *t;
    uint16_t    attr_handle;
    uint16_t    conn_handle;
    uint16_t    len = 0U;
    int         rc;
    if (argc < 4U) {
        SHELL_PRINTF("usage: bt write <handle> <text> [N]\n");
        return;
    }
    if (parse_u16(argv[2], &attr_handle) != 0) {
        SHELL_PRINTF("bt: bad handle '%s'\n", argv[2]);
        return;
    }
    conn_handle = pick_conn_handle(argc, argv, 4U);
    if (conn_handle == 0xFFFFU) {
        SHELL_PRINTF("bt: no connection\n");
        return;
    }
    for (t = argv[3]; *t != '\0' && len < sizeof buf; ++t) {
        char c = *t;
        if (c == '\\' && t[1] != '\0') {
            ++t;
            c = (*t == 'r') ? '\r' : (*t == 'n') ? '\n' : *t;
        }
        buf[len++] = (uint8_t)c;
    }
    rc = tiku_bt_client_write(conn_handle, attr_handle, buf, len);
    SHELL_PRINTF("bt: write of %u B requested (rc=%d)\n", len, rc);
}

/**
 * @brief Handle "bt subscribe <cccd> [N]": enable notifications on link N.
 */
static void bt_subscribe_cmd(uint8_t argc, const char *argv[])
{
    uint16_t cccd_handle;
    uint16_t conn_handle;
    int      rc;
    if (argc < 3U) {
        SHELL_PRINTF("usage: bt subscribe <cccd_handle> [N]\n");
        return;
    }
    if (parse_u16(argv[2], &cccd_handle) != 0) {
        SHELL_PRINTF("bt: bad handle '%s'\n", argv[2]);
        return;
    }
    conn_handle = pick_conn_handle(argc, argv, 3U);
    if (conn_handle == 0xFFFFU) {
        SHELL_PRINTF("bt: no connection\n");
        return;
    }
    rc = tiku_bt_client_subscribe(conn_handle, cccd_handle);
    SHELL_PRINTF("bt: subscribe requested (rc=%d)\n", rc);
}

/**
 * @brief Handle "bt disconnect [N]": tear down link N, or the first link.
 */
static void bt_disconnect(uint8_t argc, const char *argv[])
{
    int rc;
    uint16_t handle = 0xFFFFU;        /* default: first active link */
    if (argc >= 3U) {
        /* Parse decimal slot number (1-based). */
        const char *s = argv[2];
        uint16_t n_arg = 0U;
        while (*s >= '0' && *s <= '9') {
            n_arg = (uint16_t)(n_arg * 10U + (uint16_t)(*s - '0'));
            ++s;
        }
        if (n_arg == 0U) {
            SHELL_PRINTF("usage: bt disconnect [N]   (N = 1-based slot)\n");
            return;
        }
        {
            tiku_bt_connection_t conns[TIKU_BT_CONN_MAX];
            uint8_t n = tiku_bt_connections(conns,
                                                  TIKU_BT_CONN_MAX);
            if (n_arg > n) {
                SHELL_PRINTF("bt: no connection %u (have %u)\n",
                             n_arg, n);
                return;
            }
            handle = conns[n_arg - 1U].handle;
        }
    }
    rc = tiku_bt_disconnect(handle);
    if (rc == 0) {
        SHELL_PRINTF("bt: disconnect requested\n");
    } else {
        SHELL_PRINTF("bt: disconnect FAILED rc=%d\n", rc);
    }
}

/* ---- Bonding helpers ----------------------------------------------------- */

/**
 * @brief Handle "bt bonds": list the stored LE Secure Connections bonds.
 *
 * The Key column is the first 4 bytes of SHA-256 of the bond's LTK; the LTK
 * itself is not printed.
 */
static void bt_bonds(void)
{
    uint8_t slot;
    uint8_t shown = 0U;
    SHELL_PRINTF(" ##  Peer              Type    Key       Flags\n");
    for (slot = 0U; slot < TIKU_BT_BOND_MAX; ++slot) {
        tiku_bt_bond_record_t rec;
        int rc = tiku_bt_bond_load(slot, &rec);
        if (rc != 0) continue;
        if (rec.magic != TIKU_BT_BOND_MAGIC) continue;
        ++shown;
        SHELL_PRINTF("%3u  ", slot);
        {
            uint8_t k;
            for (k = 0U; k < 6U; ++k) {
                if (k > 0U) tiku_shell_io_putc(':');
                put_hex2(rec.peer_addr[k]);
            }
        }
        SHELL_PRINTF(" %s  ", bt_addr_type_name(rec.peer_addr_type));
        {
            uint8_t digest[TIKU_KITS_CRYPTO_SHA256_DIGEST_SIZE];
            uint8_t k;
            tiku_kits_crypto_sha256_hash(rec.ltk, sizeof(rec.ltk), digest);
            for (k = 0U; k < 4U; ++k) put_hex2(digest[k]);
        }
        SHELL_PRINTF("  0x");
        put_hex4((uint16_t)((rec.flags >> 16) & 0xFFFFU));
        put_hex4((uint16_t)(rec.flags & 0xFFFFU));
        tiku_shell_io_putc('\n');
    }
    if (shown == 0U) {
        SHELL_PRINTF("(no bonds stored)\n");
    }
}

/**
 * @brief Handle "bt unpair <N>|all": clear bond slot N, or every slot.
 */
static void bt_unpair_cmd(uint8_t argc, const char *argv[])
{
    uint16_t v;
    uint8_t  slot;
    int      rc = 0;
    if (argc < 3U) {
        SHELL_PRINTF("usage: bt unpair <N>|all   (N = slot from 'bt bonds')\n");
        return;
    }
    if (str_eq(argv[2], "all")) {
        for (slot = 0U; slot < TIKU_BT_BOND_MAX; ++slot) {
            if (tiku_bt_bond_clear(slot) != 0) rc = -1;
        }
        SHELL_PRINTF(rc == 0 ? "bt: all %u bond slots cleared\n"
                             : "bt: unpair FAILED (of %u slots)\n",
                     (unsigned)TIKU_BT_BOND_MAX);
        return;
    }
    if (parse_u16(argv[2], &v) != 0 || v >= TIKU_BT_BOND_MAX) {
        SHELL_PRINTF("bt: bad slot '%s' (range 0..%u, or all)\n",
                     argv[2], (unsigned)TIKU_BT_BOND_MAX - 1U);
        return;
    }
    slot = (uint8_t)v;
    rc = tiku_bt_bond_clear(slot);
    if (rc == 0) {
        SHELL_PRINTF("bt: bond slot %u cleared\n", slot);
    } else {
        SHELL_PRINTF("bt: unpair FAILED rc=%d\n", rc);
    }
}

/**
 * @brief Handle "bt list": print the cached LE scan results.
 *
 * Fetches up to TIKU_BT_SCAN_MAX entries via tiku_bt_scan_results() and
 * prints a table of address, RSSI, advertising event type, and device
 * name; notes when no devices have been found.
 */
static void bt_list(void)
{
    tiku_bt_scan_entry_t entries[TIKU_BT_SCAN_MAX];
    uint8_t n = tiku_bt_scan_results(entries,
                                           TIKU_BT_SCAN_MAX);
    uint8_t i;
    if (n == 0U) {
        SHELL_PRINTF("(no devices found yet -- try 'bt scan')\n");
        return;
    }
    SHELL_PRINTF(" ##  Addr               RSSI  Type             Name\n");
    for (i = 0U; i < n; ++i) {
        SHELL_PRINTF("%3u  ", i + 1U);
        {
            uint8_t k;
            for (k = 0U; k < 6U; ++k) {
                if (k > 0U) tiku_shell_io_putc(':');
                put_hex2(entries[i].addr[k]);
            }
        }
        SHELL_PRINTF(" %4d  %-16s ",
                     (int)entries[i].rssi_dbm,
                     evt_type_name(entries[i].evt_type));
        if (entries[i].name_len > 0U) {
            put_name(entries[i].name, entries[i].name_len);
        } else {
            SHELL_PRINTF("-");
        }
        tiku_shell_io_putc('\n');
    }
}

#if BT_UART
/*---------------------------------------------------------------------------*/
/* bt uart: the shell over the serial facade                                 */
/*---------------------------------------------------------------------------*/

/** Ctrl+C / ETX -- stops the session from the local console. */
#define BT_UART_CANCEL 0x03

/* Shell output gathers here between flushes; input is read a write at a
 * time.  Both ride the BLE serial facade (the Nordic UART Service). */
static uint8_t  s_out[244];
static uint16_t s_out_len;
static uint8_t  s_in[64];
static uint8_t  s_in_len;
static uint8_t  s_in_pos;

/** @brief Hand the buffered output to the pipe; a dead link drops it. */
static void bt_uart_flush(void)
{
    if (s_out_len > 0U) {
        (void)tiku_ble_serial_send(s_out, s_out_len);
        s_out_len = 0U;
    }
}

static void bt_uart_putc(char c)
{
    if (s_out_len >= (uint16_t)sizeof(s_out)) {
        bt_uart_flush();
    }
    s_out[s_out_len++] = (uint8_t)c;
}

static uint8_t bt_uart_rx_ready(void)
{
    if (s_in_pos < s_in_len) {
        return 1U;
    }
    {
        int n = tiku_ble_serial_recv(s_in, (uint16_t)sizeof(s_in));
        s_in_len = (uint8_t)((n > 0) ? n : 0);
        s_in_pos = 0U;
    }
    return (uint8_t)(s_in_len > 0U);
}

static int bt_uart_getc(void)
{
    if (!bt_uart_rx_ready()) {
        return -1;
    }
    return (int)s_in[s_in_pos++];
}

/* The shell's io backend over the pipe: output to TX notifications, input
 * from RX writes, \n sent as \r\n, no local echo (the central shows what
 * it sent), and a remote channel's restricted capability, as over TCP. */
static const tiku_shell_io_t s_bt_uart_io = {
    bt_uart_putc,              /* putc     */
    bt_uart_rx_ready,          /* rx_ready */
    bt_uart_getc,              /* getc     */
    TIKU_SHELL_IO_CRLF,        /* flags    */
    TIKU_VFS_CAP_NONE          /* remote channel: restricted, like TCP */
};

/**
 * @brief Over the pipe, print @p emit and/or run @p line, then the prompt;
 *        the console backend comes back before the output goes.
 */
static void bt_uart_run(const tiku_shell_io_t *console, const char *emit,
                        char *line)
{
    tiku_shell_io_set_backend(&s_bt_uart_io);
    if (emit != (const char *)0) {
        tiku_shell_io_puts(emit);
    }
    if (line != (char *)0 && line[0] != '\0') {
        tiku_shell_parser_execute(line);
    }
    tiku_shell_io_printf("tikuOS:%s> ", tiku_shell_cwd_get());
    tiku_shell_io_set_backend(console);
    bt_uart_flush();
}

/**
 * @brief "bt uart [name]": the shell over BLE until Ctrl-C on the console.
 *
 * Advertises connectably; once a central subscribes to TX, its RX writes
 * are shell input and the output comes back as TX notifications.
 */
static void bt_uart(uint8_t argc, const char *argv[])
{
    const char *name = (argc >= 3U) ? argv[2] : "tikuOS";
    const tiku_shell_io_t *console = tiku_shell_io_get_backend();
    static char line[128];
    uint16_t lpos = 0U;
    uint8_t  greeted = 0U;
    uint8_t  linked = 0U;
    tiku_clock_time_t beat;
    /* WFI between passes: light idle, as a controller on this core (the
     * ESP32-C61's) needs it awake; an off-chip one keeps the link alone. */
    tiku_cpu_idle_enter_t idle = tiku_cpu_idle_hook(TIKU_CPU_IDLE_LIGHT);
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
    struct tiku_process *owner = tiku_current_process;
#endif

    s_out_len = 0U;
    s_in_len = s_in_pos = 0U;
    if (tiku_ble_serial_start(name) != 0) {
        SHELL_PRINTF("bt uart: start FAILED (bt status shows the radio)\n");
        return;
    }
    SHELL_PRINTF("bt: advertising as \"%s\" (connectable)\n", name);
    SHELL_PRINTF("    connect in nRF Connect, open its UART view, then type "
                 "commands.\n    Ctrl-C here stops the wireless shell.\n");

    beat = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND);
    for (;;) {
        /* ready() pumps the stack, and turns true once a subscriber has
         * settled (notifications sent sooner are discarded). */
        uint8_t ready = (uint8_t)(tiku_ble_serial_ready() != 0);
        uint8_t now_linked = (uint8_t)(tiku_ble_serial_connected() != 0);

        if (now_linked != linked) {
            SHELL_PRINTF(now_linked ? "\nbt: CONNECTED\n"
                                    : "\nbt: DISCONNECTED -- "
                                      "re-advertising\n");
            linked = now_linked;
            greeted = 0U;
            lpos = 0U;
        }
        if (!greeted && ready) {
            bt_uart_run(console,
                        "\r\ntikuOS wireless shell -- type 'help'\r\n", 0);
            greeted = 1U;
            SHELL_PRINTF("bt: wireless shell active (subscriber attached)\n");
        }

        /* Feed RX into the line buffer; run it at a newline. */
        while (bt_uart_rx_ready()) {
            int c = bt_uart_getc();
            if (c < 0) {
                break;
            }
            if (c == '\r' || c == '\n') {
                line[lpos] = '\0';
                lpos = 0U;
                bt_uart_run(console, 0, line);
            } else if (c == 0x08 || c == 0x7F) {          /* backspace */
                if (lpos > 0U) {
                    lpos--;
                }
            } else if (lpos < (uint16_t)(sizeof(line) - 1U)) {
                line[lpos++] = (char)c;
            }
        }

        /* Ctrl-C on the console stops the session (read the console's own
         * backend: the active one is the pipe while a line runs). */
        if (console && console->rx_ready && console->getc &&
            console->rx_ready()) {
            if (console->getc() == BT_UART_CANCEL) {
                break;
            }
        }

        /* A dot a second while waiting for a central. */
        if (TIKU_CLOCK_LT(beat, tiku_clock_time())) {
            if (!linked) {
                SHELL_PRINTF(".");
            }
            beat = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND);
        }

#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
        /* The session holds the kernel thread inside one dispatch: the
         * other processes run their events here, and while none can, the
         * workers (the ESP32-C61's controller tasks among them) get the CPU
         * until the next tick or event. */
        while (tiku_process_run_except(owner)) { }
        tiku_current_process = owner;
        tiku_atomic_enter();
        if (!tiku_process_queue_dispatchable_except(owner) &&
            tiku_thread_worker_ready()) {
            tiku_thread_kernel_block();
            tiku_atomic_exit();
            continue;
        }
        tiku_atomic_exit();
#endif
        /* Nothing waiting: sleep until the next interrupt (the tick or the
         * console), at most one tick of RX latency. */
        if (idle != (tiku_cpu_idle_enter_t)0 && !bt_uart_rx_ready()) {
            idle();
        }
    }

    tiku_shell_io_set_backend(console);
    tiku_ble_serial_stop();
    SHELL_PRINTF("\nbt: session stopped.\n");
}
#endif /* BT_UART */

#if TIKU_BLE_ADV_PRESENT
/**
 * @brief Handle "bt beacon <name> [ms]" and "bt beacon off": the broadcast
 *        facade's non-connectable beacon, as /sys/radio/beacon drives it.
 */
static void bt_beacon(uint8_t argc, const char *argv[])
{
    uint16_t ms = 0U;
    if (argc < 3U) {
        SHELL_PRINTF("usage: bt beacon <name> [ms]  |  bt beacon off\n");
        return;
    }
    if (str_eq(argv[2], "off") || str_eq(argv[2], "stop")) {
        tiku_ble_adv_stop();
        SHELL_PRINTF("bt: beacon off\n");
        return;
    }
    if (argc >= 4U && parse_u16(argv[3], &ms) != 0) {
        SHELL_PRINTF("bt: bad interval '%s' (ms)\n", argv[3]);
        return;
    }
    if (tiku_ble_adv_beacon(argv[2], ms) != 0) {
        SHELL_PRINTF("bt: beacon FAILED (the radio busy or off?)\n");
        return;
    }
    SHELL_PRINTF("bt: beacon \"%s\" every %u ms\n", argv[2],
                 (unsigned)tiku_ble_adv_interval_ms());
}
#endif /* TIKU_BLE_ADV_PRESENT */

#if TIKU_DRV_BLE_EM9305_ENABLE
/**
 * @brief Handle "bt probe": the EM9305's first contact, SPI and HCI Reset.
 *
 * It resets the die, so it waits while the stack holds the radio.  It
 * passes when STS1 reads 0xC0 and HCI Reset completes with status 0.
 */
static void bt_probe(void)
{
    tiku_em9305_probe_t p;
    int      rc;
    uint16_t i;

    if (tiku_bt_is_ready()) {
        SHELL_PRINTF("bt: the radio is up (bt off first)\n");
        return;
    }
    SHELL_PRINTF("EM9305 first-contact probe (IOM6 SPI @16MHz)...\n");
    rc = tiku_em9305_probe(&p);
    SHELL_PRINTF("  reset:  spi_init=%s RDY[init=%u low=%u high=%u final=%u]\n",
                 p.spi_rc ? "FAIL" : "ok",
                 (unsigned)p.rdy_initial, (unsigned)p.saw_low,
                 (unsigned)p.saw_high, (unsigned)p.rdy_final);
    if (p.reset_rc != TIKU_EM9305_OK) {
        SHELL_PRINTF("  reset:  FAIL (rc=%d) -- radio never signalled ready\n",
                     p.reset_rc);
        return;
    }
    SHELL_PRINTF("  reset:  ok (RDY handshake completed)\n");
    SHELL_PRINTF("  SPI:    STS1=0x%02x STS2=0x%02x  [%s]\n",
                 (unsigned)p.sts1, (unsigned)p.sts2,
                 (p.sts1 == 0xC0U) ? "PASS: SPI talks to the radio"
                                   : "FAIL: no 0xC0 ready status");
    SHELL_PRINTF("  boot:   active-state event %s\n",
                 p.active_evt ? "seen (04 FF 01 01)" : "NOT seen");
    if (p.cc_seen) {
        SHELL_PRINTF("  HCI:    Reset -> Command Complete, status=0x%02x  "
                     "[%s]\n", (unsigned)p.hci_status,
                     (p.hci_status == 0U) ? "PASS" : "returned error");
    } else {
        SHELL_PRINTF("  HCI:    Reset send_rc=%d recv_rc=%d -- no Command "
                     "Complete  [FAIL]\n", (int)p.send_rc, (int)p.recv_rc);
    }
    if (p.evt_len) {
        SHELL_PRINTF("  event: ");
        for (i = 0U; i < p.evt_len; i++) {
            SHELL_PRINTF(" %02x", (unsigned)p.evt[i]);
        }
        SHELL_PRINTF("\n");
    }
    /* OK only when every line above passed: the SPI status, a Command
     * Complete, and a zero HCI status. */
    SHELL_PRINTF("%s\n", (rc == TIKU_EM9305_OK && p.sts1 == 0xC0U &&
                            p.cc_seen && p.hci_status == 0U)
                 ? "bt: first contact OK"
                 : "bt: first contact incomplete -- see above");
}
#endif /* TIKU_DRV_BLE_EM9305_ENABLE */

/*---------------------------------------------------------------------------*/

void tiku_shell_cmd_bt(uint8_t argc, const char *argv[])
{
    if (argc < 2U || str_eq(argv[1], "help")) {
        bt_help();
        return;
    }
#if (TIKU_BT_ON_DEMAND + 0)
    if (str_eq(argv[1], "on") || str_eq(argv[1], "off")) {
        int rc = tiku_bt_power(str_eq(argv[1], "on") ? 1U : 0U);

        SHELL_PRINTF(rc == 0 ? "BT: %s\n" : "BT: %s failed (%d)\n",
                     argv[1], rc);
        return;
    }
#endif
    if (str_eq(argv[1], "status")) {
        bt_status();
        return;
    }
    if (str_eq(argv[1], "advertise") || str_eq(argv[1], "adv")) {
        bt_advertise(argc, argv);
        return;
    }
    if (str_eq(argv[1], "scan")) {
        bt_scan(argc, argv);
        return;
    }
    if (str_eq(argv[1], "list")) {
        bt_list();
        return;
    }
    if (str_eq(argv[1], "connections") || str_eq(argv[1], "conns")) {
        bt_connections();
        return;
    }
    if (str_eq(argv[1], "disconnect")) {
        bt_disconnect(argc, argv);
        return;
    }
    if (str_eq(argv[1], "connect")) {
        bt_connect_cmd(argc, argv);
        return;
    }
    if (str_eq(argv[1], "discover")) {
        bt_discover_cmd(argc, argv);
        return;
    }
    if (str_eq(argv[1], "read")) {
        bt_read_cmd(argc, argv);
        return;
    }
    if (str_eq(argv[1], "write")) {
        bt_write_cmd(argc, argv);
        return;
    }
    if (str_eq(argv[1], "subscribe") || str_eq(argv[1], "sub")) {
        bt_subscribe_cmd(argc, argv);
        return;
    }
    if (str_eq(argv[1], "pair")) {
        bt_pair_cmd(argc, argv);
        return;
    }
    if (str_eq(argv[1], "bonds")) {
        bt_bonds();
        return;
    }
    if (str_eq(argv[1], "unpair")) {
        bt_unpair_cmd(argc, argv);
        return;
    }
#if BT_UART
    if (str_eq(argv[1], "uart")) {
        bt_uart(argc, argv);
        return;
    }
#endif
#if TIKU_BLE_ADV_PRESENT
    if (str_eq(argv[1], "beacon")) {
        bt_beacon(argc, argv);
        return;
    }
#endif
#if TIKU_DRV_BLE_EM9305_ENABLE
    if (str_eq(argv[1], "probe")) {
        bt_probe();
        return;
    }
#endif
    SHELL_PRINTF("bt: unknown subcommand '%s' (try 'bt help')\n", argv[1]);
}
