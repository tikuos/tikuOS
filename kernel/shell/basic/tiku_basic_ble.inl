/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_ble.inl - Bluetooth Low Energy words for BASIC.
 *
 * A tiny BLE vocabulary built on the driver-agnostic facades in
 * interfaces/bluetooth/ -- so these are GENERAL words, not tied to any one
 * radio.  Two independent capabilities light up their own words:
 *
 * TIKU_HAS_BLE (connection-capable: tiku_ble_serial, EM9305 today):
 *   BLEADV name$      start connectable advertising as a BLE serial peripheral
 *   BLESEND s$        send a string to the connected central (flow-controlled)
 *   BLEUP             function (call.inl): 1 when a central is connected+subscribed
 *   BLEAVAIL          function (call.inl): 1 when received bytes are waiting
 *   BLEGET$           function (string.inl): pop bytes the central sent, "" if none
 *
 * TIKU_HAS_BLE_ADV (broadcast: tiku_ble_adv, nRF54L15 on-die radio today):
 *   BLEBEACON name$[,ms[,data$]]  BACKGROUND non-connectable beacon (kernel
 *                         timer; survives RUN ending; board beacons while
 *                         sleeping).  data$ rides in the manufacturer data
 *                         ('TK' company id) -- the broadcast-sensor path
 *   BLESCAN$(secs)    function (string.inl): passive scan -> "addr,rssi,name;"
 *
 * Both:
 *   BLEOFF            stop advertising / beaconing / drop the link
 *   BLEBEACON on a serial-only build maps to its connectionless beacon.
 *
 * Idiomatic loop (run at the UART/USB console; BLE is the data channel):
 *   10 BLEADV "tikuOS"
 *   20 IF BLEUP()=0 THEN 25 ELSE 30    ' wait for a phone to connect + subscribe
 *   25 DELAY 100 : GOTO 20
 *   30 BLESEND "hello from BASIC"
 *   40 IF BLEAVAIL() THEN PRINT "rx: "; BLEGET$()
 *   50 DELAY 50 : GOTO 40
 *
 * Two rules the loop above encodes:
 *  - Gate BLEGET$ on BLEAVAIL().  BASIC's string heap is not GC'd within a RUN,
 *    so `A$=BLEGET$()` every pass (even of "") would exhaust it; BLEAVAIL() is a
 *    numeric predicate that allocates nothing, so we only read when data waits.
 *  - Pace with DELAY.  A RUN is bounded by a 100k-statement anti-runaway cap
 *    (tiku_basic_run.inl); a tight poll spins through it in seconds, so a real
 *    responder paces itself (and a persistent one is better written with EVERY).
 *
 * Cooperative-blocking rule (see tiku_basic_net.inl): the only word that waits
 * is BLESEND, and its wait is the facade's bounded credit-drain -- it kicks the
 * watchdog and gives up after a one-second stall, so it cannot hard-hang the
 * board.  BLEUP/BLEAVAIL/BLEGET$ poll the stack once and return, which is also
 * what keeps the link serviced inside a BASIC poll loop.
 *
 * NOT a standalone translation unit -- included from tiku_basic.c after
 * tiku_basic_net.inl (reuses parse_strexpr, basic_error, SHELL_PRINTF).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_BLE_ENABLE

/* Advertised names ride in a 31-byte LE adv PDU (Flags + Complete Local Name),
 * so anything past ~26 chars would be truncated by the radio anyway. */
#define BASIC_BLE_NAME_CAP  24

#if TIKU_BLE_SERIAL_PRESENT
/* BLEADV ["name"] -- advertise connectably as a BLE serial peripheral.  A bare
 * BLEADV (or "") uses the default name.  Connection-capable backends only. */
static void
exec_bleadv(const char **p)
{
    char        name[BASIC_BLE_NAME_CAP];
    const char *nm;
    skip_ws(p);
    if (**p == '\0' || **p == ':') {        /* bare BLEADV -> default name */
        name[0] = '\0';
    } else if (parse_strexpr(p, name, sizeof(name)) != 0) {
        return;
    }
    nm = (name[0] != '\0') ? name : "tikuOS";
    if (tiku_ble_serial_start(nm) != 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? BLE start failed (radio present?)\n" SH_RST);
    }
}

/* BLESEND expr$ -- send a string to the connected central. */
static void
exec_blesend(const char **p)
{
    char s[TIKU_BASIC_STR_BUF_CAP];
    if (parse_strexpr(p, s, sizeof(s)) != 0) {
        return;
    }
    if (tiku_ble_serial_send((const uint8_t *)s, (uint16_t)strlen(s)) < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED
            "? BLE not connected (check BLEUP() before BLESEND)\n" SH_RST);
    }
}
#endif /* TIKU_BLE_SERIAL_PRESENT */

/* BLEOFF -- stop advertising / beaconing and drop any link. */
static void
exec_bleoff(const char **p)
{
    (void)p;
#if TIKU_BLE_SERIAL_PRESENT
    tiku_ble_serial_stop();
#endif
#if TIKU_BLE_ADV_PRESENT
    tiku_ble_adv_stop();
#endif
}

/* BLEBEACON ["name"][,interval_ms[,data$]] -- start a non-connectable
 * beacon.
 *
 * On a broadcast backend (tiku_ble_adv) the beacon is a BACKGROUND kernel
 * timer: RUN can end and the board keeps advertising while it sleeps
 * (tickless), until BLEOFF.  The optional interval (default 1000 ms,
 * clamped to the BLE legal range) is the microwatt knob: energy scales
 * linearly with burst rate.
 *
 * The optional data$ is a telemetry payload carried in the manufacturer
 * data after the 'TK' company id -- the broadcast-sensor pattern:
 *   10 BLEBEACON "TIKU-T", 1000, "T=" + STR$(A)
 * makes the reading visible to any observer with no connection.  Calling
 * BLEBEACON again swaps the payload in place (also on the offloaded
 * coprocessor path). */
static void
exec_blebeacon(const char **p)
{
    char        name[BASIC_BLE_NAME_CAP];
    const char *nm;
    long        ms = 0;
    char        data[TIKU_BLE_ADV_DATA_CAP + 1];
    uint8_t     dlen = 0u;
    skip_ws(p);
    if (**p == '\0' || **p == ':') {
        name[0] = '\0';
    } else if (**p == ',') {
        name[0] = '\0';                     /* BLEBEACON ,500 -> default    */
    } else if (parse_strexpr(p, name, sizeof(name)) != 0) {
        return;
    }
    skip_ws(p);
    if (**p == ',') {
        (*p)++;
        ms = parse_expr(p);
        if (basic_error) return;
        if (ms < 0) ms = 0;
        if (ms > 65535) ms = 65535;
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            if (parse_strexpr(p, data, sizeof(data)) != 0) {
                return;
            }
            dlen = (uint8_t)strlen(data);
        }
    }
    nm = (name[0] != '\0') ? name : "tikuOS";
#if TIKU_BLE_ADV_PRESENT
    if (tiku_ble_adv_beacon_data(nm, (uint16_t)ms,
                                 dlen ? (const uint8_t *)data
                                      : (const uint8_t *)0, dlen) != 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? BLE beacon failed (radio present?)\n" SH_RST);
    }
#else
    (void)data; (void)dlen;                 /* no payload slot over serial  */
    (void)ms;                               /* serial backends pick their own */
    if (tiku_ble_serial_beacon(nm) != 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? BLE beacon failed (radio present?)\n" SH_RST);
    }
#endif
}

#endif /* TIKU_BASIC_BLE_ENABLE */
