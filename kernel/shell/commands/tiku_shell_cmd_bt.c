/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_bt.c - "bt" command implementation
 *
 * Subcommands:
 *   bt status   — chip BT MAC + HCI/LMP version + BTFW string
 *   bt help     — usage
 *
 * Implementation just glues to interfaces/bluetooth/tiku_bt.h's public API.
 * No driver state lives in shell code.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_bt.h"
#include <kernel/shell/tiku_shell.h>
#include <interfaces/bluetooth/tiku_bt.h>

/*---------------------------------------------------------------------------*/

/**
 * @brief Compare two C strings for exact equality (1 if equal, else 0).
 */
static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

/* SHELL_PRINTF doesn't honour %02x / %04x zero-padding; emit hex
 * nibbles directly so MAC bytes + 16-bit identifiers stay aligned. */
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
    SHELL_PRINTF("bt status               CYW43 BT subsystem: MAC + version\n");
    SHELL_PRINTF("bt advertise <name>     Start GAP advertising with local name\n");
    SHELL_PRINTF("bt advertise stop       Stop GAP advertising\n");
    SHELL_PRINTF("bt scan                 Start LE scan (clears cache)\n");
    SHELL_PRINTF("bt scan stop            Stop LE scan\n");
    SHELL_PRINTF("bt list                 Print cached scan results\n");
    SHELL_PRINTF("bt connections          List active LE links\n");
    SHELL_PRINTF("bt disconnect [N]       Tear down link N (default: first)\n");
    SHELL_PRINTF("bt connect <slot|addr>  Initiate central-role connection\n");
    SHELL_PRINTF("bt discover [N]         Service discovery on link N\n");
    SHELL_PRINTF("bt read <handle> [N]    ATT Read on link N\n");
    SHELL_PRINTF("bt subscribe <h> [N]    Write 0x0001 to CCCD on link N\n");
    SHELL_PRINTF("bt bonds                List stored LE-SC bonds (LTK slots)\n");
    SHELL_PRINTF("bt unpair [N]           Clear bond slot N (default 0)\n");
    SHELL_PRINTF("bt help                 this help\n");
}

/* HCI version code -> human-readable Bluetooth Core Spec name.
 * Table per Assigned Numbers / Core Spec "HCI_Version" values. */
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
 * @brief Handle "bt status": print the CYW43 BT subsystem information.
 *
 * When the controller is ready, prints the BD_ADDR, HCI and LMP versions
 * (with Core Spec names), manufacturer, firmware string, and current
 * advertising/scanning state.
 */
static void bt_status(void)
{
    if (tiku_bt_is_ready() == 0) {
        SHELL_PRINTF("BT: not ready (bring-up failed or not built in)\n");
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

static void bt_scan(uint8_t argc, const char *argv[])
{
    int rc;
    if (argc >= 3U && str_eq(argv[2], "stop")) {
        rc = tiku_bt_scan_stop();
        SHELL_PRINTF("bt: scan stopped (rc=%d, %u devices)\n",
                     rc, tiku_bt_scan_count());
        return;
    }
    /* Defaults: active scan, 100 ms interval, 50 ms on-window
     * (50% duty). Roughly matches nRF Connect's default. */
    rc = tiku_bt_scan_start(1U, 100U, 50U);
    if (rc == 0) {
        SHELL_PRINTF("bt: scan started (active, 100/50 ms)\n");
    } else {
        SHELL_PRINTF("bt: scan start FAILED rc=%d\n", rc);
    }
}

/* Map LE advertising event-type code to a short label. */
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
        /* SHELL_PRINTF doesn't always honour %-Ns width; emit the
         * fixed-column fields with %s then pad manually. */
        SHELL_PRINTF(" %s  ", bt_addr_type_name(conns[i].peer_addr_type));
        SHELL_PRINTF("%s   0x",
                     conns[i].role == 1U ? "peripheral" : "central   ");
        put_hex4(conns[i].handle);
        SHELL_PRINTF("  %lu.%lu ms\n",
                     (unsigned long)(interval_us / 1000UL),
                     (unsigned long)((interval_us % 1000UL) / 100UL));
    }
}

/* ---- Phase 13 client-side shell helpers ---------------------------------- */

/** Parse "aa:bb:cc:dd:ee:ff" into 6 MSB-first bytes. Returns 0 on success. */
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

/** Parse a 0xNNNN or decimal NNN into uint16_t. Returns 0 on success. */
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

/** Look up connection #N (1-based). Returns the handle or 0xFFFF on miss. */
static uint16_t conn_handle_for_slot(uint8_t slot_1based)
{
    tiku_bt_connection_t conns[TIKU_BT_CONN_MAX];
    uint8_t n = tiku_bt_connections(conns, TIKU_BT_CONN_MAX);
    if (slot_1based == 0U || slot_1based > n) return 0xFFFFU;
    return conns[slot_1based - 1U].handle;
}

/** Pick the conn handle for an optional shell arg "N" (1-based) or
 *  default to first active link. Prints + returns 0xFFFF on error. */
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
        /* Default to random addr type unless the user explicitly
         * appends ' public'. Most modern BLE devices use random. */
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

/* ---- Phase 14 bonding shell helpers -------------------------------------- */

static void bt_bonds(void)
{
    uint8_t slot;
    uint8_t shown = 0U;
    SHELL_PRINTF(" ##  Peer               Type    LTK"
                 "                              Flags\n");
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
            uint8_t k;
            for (k = 0U; k < 16U; ++k) put_hex2(rec.ltk[k]);
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

static void bt_unpair_cmd(uint8_t argc, const char *argv[])
{
    uint8_t slot = 0U;
    int     rc;
    if (argc >= 3U) {
        uint16_t v;
        if (parse_u16(argv[2], &v) != 0 || v >= TIKU_BT_BOND_MAX) {
            SHELL_PRINTF("bt: bad slot '%s' (range 0..%u)\n",
                         argv[2], (unsigned)TIKU_BT_BOND_MAX - 1U);
            return;
        }
        slot = (uint8_t)v;
    }
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

/*---------------------------------------------------------------------------*/

void tiku_shell_cmd_bt(uint8_t argc, const char *argv[])
{
    if (argc < 2U || str_eq(argv[1], "help")) {
        bt_help();
        return;
    }
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
    if (str_eq(argv[1], "subscribe") || str_eq(argv[1], "sub")) {
        bt_subscribe_cmd(argc, argv);
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
    SHELL_PRINTF("bt: unknown subcommand '%s' (try 'bt help')\n", argv[1]);
}
