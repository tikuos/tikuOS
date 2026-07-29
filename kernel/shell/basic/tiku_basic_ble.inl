/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_ble.inl - Bluetooth Low Energy words for BASIC.
 *
 * General words built on the driver-agnostic facades, not tied to one radio.  Two
 * independent capabilities light up their own vocabularies: connection-capable
 * serial words, and broadcast beacon and scan words.
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
    if (cur_peek(p) == '\0' || cur_peek(p) == ':') {        /* bare BLEADV -> default name */
        name[0] = '\0';
    } else if (parse_strexpr(p, name, sizeof(name)) != 0) {
        return;
    }
    nm = (name[0] != '\0') ? name : "tikuOS";
    if (tiku_ble_serial_start(nm) != 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "BLE start failed (radio present?)");
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
        basic_throw(TIKU_BASIC_ERR_GENERAL, "BLE not connected (check BLEUP() before BLESEND)");
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

/* BLEBEACON ["name"][,interval_ms[,data$[,dbm]]] -- start a
 * non-connectable beacon.
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
 * coprocessor path).
 *
 * The optional dbm is the second microwatt knob: TX power in signed dBm,
 * discrete silicon steps only (+8..-46 on nRF54L; an illegal step throws
 * rather than rounding).  Broadcast backends only; serial backends parse
 * and ignore it. */
static void
exec_blebeacon(const char **p)
{
    char        name[BASIC_BLE_NAME_CAP];
    const char *nm;
    long        ms = 0;
    char        data[TIKU_BLE_ADV_DATA_CAP + 1];
    uint8_t     dlen = 0u;
    long        dbm = 0;
    uint8_t     have_dbm = 0u;
    skip_ws(p);
    if (cur_peek(p) == '\0' || cur_peek(p) == ':') {
        name[0] = '\0';
    } else if (cur_peek(p) == ',') {
        name[0] = '\0';                     /* BLEBEACON ,500 -> default    */
    } else if (parse_strexpr(p, name, sizeof(name)) != 0) {
        return;
    }
    skip_ws(p);
    if (cur_peek(p) == ',') {
        cur_advance(p);
        ms = parse_expr(p);
        if (basic_error) return;
        if (ms < 0) ms = 0;
        if (ms > 65535) ms = 65535;
        skip_ws(p);
        if (cur_peek(p) == ',') {
            cur_advance(p);
            if (parse_strexpr(p, data, sizeof(data)) != 0) {
                return;
            }
            dlen = (uint8_t)strlen(data);
            skip_ws(p);
            if (cur_peek(p) == ',') {
                cur_advance(p);
                dbm = parse_expr(p);
                if (basic_error) return;
                have_dbm = 1u;
            }
        }
    }
    nm = (name[0] != '\0') ? name : "tikuOS";
#if TIKU_BLE_ADV_PRESENT
    if (have_dbm &&
        (dbm < -128 || dbm > 127 ||
         tiku_ble_adv_set_txpower((int8_t)dbm) != 0)) {
        basic_throw(TIKU_BASIC_ERR_GENERAL,
                    "bad TX power (discrete dBm steps only)");
        return;
    }
    if (tiku_ble_adv_beacon_data(nm, (uint16_t)ms,
                                 dlen ? (const uint8_t *)data
                                      : (const uint8_t *)0, dlen) != 0) {
        basic_throw(TIKU_BASIC_ERR_NET, "BLE beacon failed (radio present?)");
    }
#else
    (void)data; (void)dlen;                 /* no payload slot over serial  */
    (void)ms;                               /* serial backends pick their own */
    (void)dbm; (void)have_dbm;              /* no power knob over serial    */
    if (tiku_ble_serial_beacon(nm) != 0) {
        basic_throw(TIKU_BASIC_ERR_NET, "BLE beacon failed (radio present?)");
    }
#endif
}

#if TIKU_BLE_ADV_PRESENT
/* BLEOBSERVE [secs] | BLEOBSERVE OFF -- background observer (R7).
 *
 * Non-blocking radio awareness: the IRQ+hardware-window engine scans
 * while the program keeps running (and after RUN ends), filling a
 * 12-slot dedup table read back with BLESEEN() / BLESEEN$(i).  secs
 * 0/absent = until BLEOBSERVE OFF (or BLEOFF? no -- BLEOFF is the
 * beacon's; the observer has its own OFF so the two never surprise
 * each other).  The ownership arbiter applies: starting while a beacon
 * runs throws (one radio, one owner).
 *
 * The agent loop this enables -- react to the radio environment
 * without ever blocking:
 *   10 BLEOBSERVE 0
 *   20 IF BLESEEN() = 0 THEN DELAY 200 : GOTO 20
 *   30 PRINT "heard: "; BLESEEN$(0)
 *   40 BLEOBSERVE OFF
 */
static void
exec_bleobserve(const char **p)
{
    long secs = 0;
    skip_ws(p);
    if (match_kw(p, "OFF")) {
        tiku_ble_adv_observe_stop();
        return;
    }
    if (cur_peek(p) != '\0' && cur_peek(p) != ':') {
        secs = parse_expr(p);
        if (basic_error) return;
        if (secs < 0) secs = 0;
        if (secs > 3600) secs = 3600;
    }
    if (tiku_ble_adv_observe_start((uint16_t)secs) != 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL,
                    "radio busy (BLEOFF the beacon first)");
    }
}
#endif /* TIKU_BLE_ADV_PRESENT */

#endif /* TIKU_BASIC_BLE_ENABLE */
