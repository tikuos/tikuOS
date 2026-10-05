/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_ble.inl - Bluetooth Low Energy words for BASIC.
 *
 * Words built on the driver-agnostic BLE facades.  Two capabilities each bring
 * their own words: connection-capable serial (TIKU_BLE_SERIAL_PRESENT) and
 * broadcast beacon and scan (TIKU_BLE_ADV_PRESENT).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_BLE_ENABLE

/* Name buffer, NUL included: a 31-byte LE advertising payload, less the
 * Flags field and the name header, holds at most 26 name bytes. */
#define BASIC_BLE_NAME_CAP  24

#if TIKU_BLE_SERIAL_PRESENT
/**
 * @brief BLEADV ["name"]: advertise connectably as a BLE serial peripheral.
 *
 * A bare BLEADV (or "") uses the default name.  Connection-capable backends
 * only.
 */
static void
exec_bleadv(const char **p)
{
    BASIC_RECLAIM_EXTERNAL();
    char        name[BASIC_BLE_NAME_CAP];
    const char *nm;
    skip_ws(p);
    if (cur_peek(p) == '\0' || cur_peek(p) == ':') {    /* bare: default name */
        name[0] = '\0';
    } else if (parse_strexpr(p, name, sizeof(name)) != 0) {
        return;
    }
    nm = (name[0] != '\0') ? name : "tikuOS";
    if (tiku_ble_serial_start(nm) != 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "BLE start failed (radio present?)");
    }
}

/** @brief BLESEND expr$: send a string to the connected central. */
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

/**
 * @brief BLEOFF: stop advertising and beaconing and drop any link.
 *
 * The background observer keeps running; BLEOBSERVE OFF stops it.
 */
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

/**
 * @brief BLEBEACON ["name"][,interval_ms[,data$[,dbm]]]: start a beacon.
 *
 * A non-connectable beacon; on a broadcast backend (tiku_ble_adv) it is a
 * background kernel timer that keeps advertising after RUN ends, until
 * BLEOFF.  Serial backends parse interval_ms, data$ and dbm and ignore them.
 */
static void
exec_blebeacon(const char **p)
{
    /* interval_ms defaults to 1000 and is clamped to the legal BLE range;
     * energy scales with the burst rate.  data$ is a telemetry payload in
     * the manufacturer data after the 'TK' company id, readable by any
     * observer without a connection, and a repeat BLEBEACON swaps it in
     * place (also when the beacon runs on the coprocessor):
     *   10 BLEBEACON "TIKU-T", 1000, "T=" + STR$(A)
     * dbm is the TX power in discrete silicon steps (+8..-46 on nRF54L); an
     * illegal step throws and is not rounded. */
    BASIC_RECLAIM_EXTERNAL();
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
/**
 * @brief BLEOBSERVE [secs] | BLEOBSERVE OFF: run the background observer.
 *
 * The radio scans while the program runs, and after RUN ends, filling the
 * dedup table BLESEEN() / BLESEEN$(i) read.  secs 0 or absent runs until
 * BLEOBSERVE OFF; BLEOFF leaves the observer running.
 *
 * @note Starting throws unless the radio is idle or runs a timer-driven
 *       beacon, which then shares it with the observer.
 */
static void
exec_bleobserve(const char **p)
{
    /* Reacting to the radio without blocking:
     *   10 BLEOBSERVE 0
     *   20 IF BLESEEN() = 0 THEN DELAY 200 : GOTO 20
     *   30 PRINT "heard: "; BLESEEN$(0)
     *   40 BLEOBSERVE OFF */
    BASIC_RECLAIM_EXTERNAL();
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
