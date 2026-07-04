/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_ble.inl - Bluetooth Low Energy words for BASIC.
 *
 * A tiny "serial over BLE" vocabulary built on the driver-agnostic facade in
 * interfaces/bluetooth/tiku_ble_serial.h -- so these are GENERAL words, not
 * tied to any one radio.  Today the only backend is the EM9305 on the Apollo510
 * Blue, but nothing here knows that; a future BLE platform lights the same
 * words up automatically via TIKU_HAS_BLE.
 *
 *   BLEADV name$      start connectable advertising as a BLE serial peripheral
 *   BLEOFF            stop advertising / drop the link
 *   BLESEND s$        send a string to the connected central (flow-controlled)
 *   BLEBEACON name$   start a non-connectable beacon
 *   BLEUP             function (call.inl): 1 when a central is connected+subscribed
 *   BLEAVAIL          function (call.inl): 1 when received bytes are waiting
 *   BLEGET$           function (string.inl): pop bytes the central sent, "" if none
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

/* BLEADV ["name"] -- advertise connectably as a BLE serial peripheral.  A bare
 * BLEADV (or "") uses the default name. */
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

/* BLEOFF -- stop advertising and drop any link. */
static void
exec_bleoff(const char **p)
{
    (void)p;
    tiku_ble_serial_stop();
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

/* BLEBEACON ["name"] -- start a non-connectable beacon. */
static void
exec_blebeacon(const char **p)
{
    char        name[BASIC_BLE_NAME_CAP];
    const char *nm;
    skip_ws(p);
    if (**p == '\0' || **p == ':') {
        name[0] = '\0';
    } else if (parse_strexpr(p, name, sizeof(name)) != 0) {
        return;
    }
    nm = (name[0] != '\0') ? name : "tikuOS";
    if (tiku_ble_serial_beacon(nm) != 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? BLE beacon failed (radio present?)\n" SH_RST);
    }
}

#endif /* TIKU_BASIC_BLE_ENABLE */
