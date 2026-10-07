/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_adv_hci.c - the broadcast facade over the host stack (tiku_bt).
 *
 * For an HCI controller: the beacon is a non-connectable advertising set the
 * controller sends itself, and scans and the observer read its reports under
 * the Nordic observer's rules (tiku_ble_adv.c).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_ble_adv.h"

#if TIKU_BLE_ADV_PRESENT && (defined(TIKU_BT_HOST) && (TIKU_BT_HOST + 0))

#include "tiku.h"                           /* the platform's tick, clock */
#include <interfaces/bluetooth/tiku_bt.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <string.h>

#define BLE_ADV_INTERVAL_DFLT_MS  1000u
#define BLE_ADV_INTERVAL_MIN_MS   100u
#define BLE_ADV_INTERVAL_MAX_MS   10240u
#define OBSERVE_MAX_REPORTS       12u

/* The beacon: on, its name, telemetry and interval, when it started, and
 * the bursts of the beacons before it. */
static uint8_t           s_beacon;
static char              s_name[TIKU_BLE_ADV_NAME_CAP + 1];
static uint8_t           s_data[TIKU_BLE_ADV_DATA_CAP];
static uint8_t           s_data_len;
static uint16_t          s_interval_ms;
static tiku_clock_time_t s_since;
static uint32_t          s_bursts_done;

/* Where advertising reports go while a scan or the observer runs. */
struct scan_ctx {
    tiku_ble_adv_report_t *out;
    uint8_t max;
    uint8_t count;
    const char *prefix;                 /* insert-time name filter (or NULL) */
    uint8_t plen;
};
static struct scan_ctx      *s_sink;
static uint8_t               s_scanning;      /* a blocking scan runs */
static uint8_t               s_dirty;         /* the table changed */

/* The background observer and the last-scan summary /sys/radio reads. */
static tiku_ble_adv_report_t bg_reports[OBSERVE_MAX_REPORTS];
static struct scan_ctx       bg_ctx;
static uint8_t               s_observing;
static struct tiku_timer     observe_timer;
static tiku_clock_time_t     observe_deadline;
static uint8_t               observe_forever;
static void                (*scan_notify_fn)(void);
static uint8_t               scan_last_count;
static uint8_t               scan_have_best;
static tiku_ble_adv_report_t scan_last_best;
static uint32_t scan_drop_ctx, scan_drop_kind, scan_drop_name, scan_kept;

/*---------------------------------------------------------------------------*/
/* THE RADIO                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief Bring the controller up if it is off (it stays up afterwards). */
static int adv_radio_up(void)
{
    if (tiku_bt_is_ready()) {
        return 0;
    }
    (void)tiku_bt_power(1u);
    return tiku_bt_is_ready() ? 0 : -1;
}

int tiku_ble_adv_available(void)
{
    return 1;
}

tiku_ble_adv_owner_t tiku_ble_adv_owner(void)
{
    /* The controller schedules its own advertising and scanning; this
     * names what runs.  A link, or advertising the beacon did not start
     * (the serial facade's), holds the radio as a connection. */
    if (tiku_bt_connection_count() > 0u ||
        (tiku_bt_is_advertising() && !s_beacon)) {
        return TIKU_BLE_ADV_OWNER_CONN;
    }
    if (s_scanning) {
        return TIKU_BLE_ADV_OWNER_SCAN;
    }
    if (s_observing) {
        return s_beacon ? TIKU_BLE_ADV_OWNER_BEACON_OBSERVE
                        : TIKU_BLE_ADV_OWNER_OBSERVE;
    }
    return s_beacon ? TIKU_BLE_ADV_OWNER_BEACON : TIKU_BLE_ADV_OWNER_IDLE;
}

const char *tiku_ble_adv_owner_str(void)
{
    static const char *names[8] = {
        "idle", "beacon", "beacon-flpr", "scan", "observe",
        "beacon+observe", "conn", "154",
    };
    return names[tiku_ble_adv_owner()];
}

/* The serial facade over the host stack needs no claim: the controller
 * runs a link beside a beacon or a scan, and owner() reports it. */
int tiku_ble_adv_conn_claim(void)
{
    return 0;
}

void tiku_ble_adv_conn_release(void)
{
}

/* No 802.15.4 on an HCI controller. */
int tiku_ble_adv_154_claim(void)
{
    return -1;
}

void tiku_ble_adv_154_release(void)
{
}

/*---------------------------------------------------------------------------*/
/* BEACON                                                                    */
/*---------------------------------------------------------------------------*/

int tiku_ble_adv_beacon(const char *name, uint16_t interval_ms)
{
    return tiku_ble_adv_beacon_data(name, interval_ms, (const uint8_t *)0,
                                    0u);
}

int tiku_ble_adv_beacon_data(const char *name, uint16_t interval_ms,
                             const uint8_t *data, uint8_t data_len)
{
    uint8_t ad[31];
    uint8_t adlen = 0u;
    uint8_t nlen;

    if (tiku_ble_adv_owner() == TIKU_BLE_ADV_OWNER_CONN ||
        s_scanning || adv_radio_up() != 0) {
        return -1;
    }
    if (name == (const char *)0 || name[0] == '\0') {
        name = "tikuOS";
    }
    nlen = (uint8_t)strlen(name);
    if (nlen > TIKU_BLE_ADV_NAME_CAP) {
        nlen = TIKU_BLE_ADV_NAME_CAP;
    }
    if (data == (const uint8_t *)0) {
        data_len = 0u;
    }
    if (data_len > TIKU_BLE_ADV_DATA_CAP) {
        data_len = TIKU_BLE_ADV_DATA_CAP;
    }
    /* The 31-byte AD budget: Flags(3) + Name(2+nlen) + Mfr(4+data_len).
     * Telemetry has priority -- the name yields when both cannot fit. */
    if ((uint8_t)(9u + nlen + data_len) > 31u) {
        nlen = (uint8_t)(31u - 9u - data_len);
    }
    if (interval_ms == 0u) {
        interval_ms = BLE_ADV_INTERVAL_DFLT_MS;
    }
    if (interval_ms < BLE_ADV_INTERVAL_MIN_MS) {
        interval_ms = BLE_ADV_INTERVAL_MIN_MS;
    }
    if (interval_ms > BLE_ADV_INTERVAL_MAX_MS) {
        interval_ms = BLE_ADV_INTERVAL_MAX_MS;
    }

    /* AD: Flags (LE general discoverable, no BR/EDR) + Complete Local Name
     * + manufacturer data: company id 0x4B54 ('TK' little-endian) followed
     * by the telemetry payload -- the Nordic beacon's layout. */
    ad[adlen++] = 0x02u; ad[adlen++] = 0x01u; ad[adlen++] = 0x06u;
    ad[adlen++] = (uint8_t)(1u + nlen);
    ad[adlen++] = 0x09u;
    memcpy(&ad[adlen], name, nlen);
    adlen = (uint8_t)(adlen + nlen);
    ad[adlen++] = (uint8_t)(3u + data_len);
    ad[adlen++] = 0xFFu; ad[adlen++] = 'T';
    ad[adlen++] = 'K';
    if (data_len) {
        memcpy(&ad[adlen], data, data_len);
        adlen = (uint8_t)(adlen + data_len);
    }
    if (tiku_bt_advertise_raw(0x03u /* ADV_NONCONN_IND */, interval_ms, ad,
                              adlen) != 0) {
        return -1;
    }

    if (s_beacon) {
        s_bursts_done = tiku_ble_adv_bursts();      /* the old one's count */
    }
    memcpy(s_name, name, nlen);
    s_name[nlen] = '\0';
    if (data_len) {
        memcpy(s_data, data, data_len);
    }
    s_data_len = data_len;
    s_interval_ms = interval_ms;
    s_since = tiku_clock_time();
    s_beacon = 1u;
    return 0;
}

void tiku_ble_adv_stop(void)
{
    if (!s_beacon) {
        return;
    }
    s_bursts_done = tiku_ble_adv_bursts();
    s_beacon = 0u;
    s_name[0] = '\0';
    s_data_len = 0u;
    s_interval_ms = 0u;
    if (tiku_bt_is_ready()) {
        (void)tiku_bt_advertise_stop();
    }
}

int tiku_ble_adv_active(void)
{
    return s_beacon;
}

const char *tiku_ble_adv_name(void)
{
    return s_name;
}

uint16_t tiku_ble_adv_interval_ms(void)
{
    return s_interval_ms;
}

uint8_t tiku_ble_adv_data(const uint8_t **out)
{
    if (out != (const uint8_t **)0) {
        *out = s_data;
    }
    return s_beacon ? s_data_len : 0u;
}

/* The controller sends the bursts on its own clock and counts none for the
 * host: one 3-channel burst per interval of the time the beacon ran. */
uint32_t tiku_ble_adv_bursts(void)
{
    if (!s_beacon || s_interval_ms == 0u) {
        return s_bursts_done;
    }
    return s_bursts_done +
           (uint32_t)(((uint64_t)(tiku_clock_time() - s_since) * 1000u) /
                      ((uint64_t)TIKU_CLOCK_SECOND * s_interval_ms));
}

/* Over HCI the controller's advertising power is fixed: the one value it
 * reports is the only legal step. */
int tiku_ble_adv_set_txpower(int8_t dbm)
{
    int8_t now;

    if (adv_radio_up() != 0 || tiku_bt_adv_tx_power(&now) != 0) {
        return -1;
    }
    return (dbm == now) ? 0 : -1;
}

int8_t tiku_ble_adv_txpower(void)
{
    int8_t now = 0;

    if (tiku_bt_is_ready()) {
        (void)tiku_bt_adv_tx_power(&now);
    }
    return now;
}

/*---------------------------------------------------------------------------*/
/* ADVERTISING REPORTS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Extract the Local Name (complete 0x09 preferred over shortened 0x08)
 *        from the AD structures.
 */
static void scan_parse_name(const uint8_t *ad, uint8_t ad_len, char *out)
{
    uint8_t i = 0u;
    while ((uint8_t)(i + 1u) < ad_len) {
        uint8_t l = ad[i];
        uint8_t t = ad[i + 1u];
        if (l == 0u || (uint8_t)(i + 1u + l) > ad_len) {
            break;
        }
        if ((t == 0x09u || t == 0x08u) && l >= 2u) {
            uint8_t n = (uint8_t)(l - 1u);
            if (n > TIKU_BLE_ADV_NAME_CAP) {
                n = TIKU_BLE_ADV_NAME_CAP;
            }
            memcpy(out, &ad[i + 2u], n);
            out[n] = '\0';
            if (t == 0x09u) {
                return;                 /* complete name wins outright      */
            }
        }
        i = (uint8_t)(i + 1u + l);
    }
}

/** @brief 'TK' manufacturer-data fallback name, used only under a filter. */
static void scan_parse_mfr_tk(const uint8_t *ad, uint8_t ad_len, char *out)
{
    uint8_t i = 0u;
    while ((uint8_t)(i + 1u) < ad_len) {
        uint8_t l = ad[i];
        uint8_t t = ad[i + 1u];
        if (l == 0u || (uint8_t)(i + 1u + l) > ad_len) {
            break;
        }
        if (t == 0xFFu && l >= 4u &&
            ad[i + 2u] == (uint8_t)'T' && ad[i + 3u] == (uint8_t)'K') {
            uint8_t n = (uint8_t)(l - 3u);
            if (n > TIKU_BLE_ADV_NAME_CAP) {
                n = TIKU_BLE_ADV_NAME_CAP;
            }
            memcpy(out, &ad[i + 4u], n);
            out[n] = '\0';
            return;
        }
        i = (uint8_t)(i + 1u + l);
    }
}

void tiku_ble_adv_scan_drops(uint32_t *ctx_bad, uint32_t *kind, uint32_t *named,
                             uint32_t *kept)
{
    if (ctx_bad != (uint32_t *)0) { *ctx_bad = scan_drop_ctx; }
    if (kind != (uint32_t *)0)    { *kind = scan_drop_kind; }
    if (named != (uint32_t *)0)   { *named = scan_drop_name; }
    if (kept != (uint32_t *)0)    { *kept = scan_kept; }
}

/**
 * @brief The stack's report hook: dedup by address, keep the strongest RSSI
 *        and the first name, into whichever table the running scan fills.
 */
static void adv_report(uint8_t evt_type, uint8_t addr_type,
                       const uint8_t addr[6], const uint8_t *ad,
                       uint8_t ad_len, int8_t rssi)
{
    /* HCI's event types as the on-air PDU types the facade reports. */
    static const uint8_t pdu_type[5] = { 0u, 1u, 6u, 2u, 4u };
    struct scan_ctx *ctx = s_sink;
    tiku_ble_adv_report_t *slot = (tiku_ble_adv_report_t *)0;
    char name[TIKU_BLE_ADV_NAME_CAP + 1];
    uint8_t i;

    (void)addr_type;
    if (ctx == (struct scan_ctx *)0 ||
        ctx->out == (tiku_ble_adv_report_t *)0 || ctx->max == 0u) {
        scan_drop_ctx++;
        return;
    }
    if (evt_type > 4u || ad_len > 31u) {
        scan_drop_kind++;
        return;
    }
    name[0] = '\0';
    if (evt_type != 1u) {                       /* DIRECT carries no AD */
        scan_parse_name(ad, ad_len, name);
        if (name[0] == '\0' && ctx->plen != 0u) {
            scan_parse_mfr_tk(ad, ad_len, name);
        }
    }
    if (ctx->plen != 0u && strncmp(name, ctx->prefix, ctx->plen) != 0) {
        scan_drop_name++;
        return;
    }
    for (i = 0u; i < ctx->count; i++) {
        if (memcmp(ctx->out[i].addr, addr, 6u) == 0) {
            slot = &ctx->out[i];
            break;
        }
    }
    if (slot == (tiku_ble_adv_report_t *)0) {
        if (ctx->count >= ctx->max) {
            return;
        }
        slot = &ctx->out[ctx->count++];
        scan_kept++;
        memcpy(slot->addr, addr, 6u);
        slot->rssi = rssi;
        slot->adv_type = pdu_type[evt_type];
        slot->name[0] = '\0';
    } else if (rssi > slot->rssi) {
        slot->rssi = rssi;
    }
    if (slot->name[0] == '\0' && name[0] != '\0') {
        memcpy(slot->name, name, sizeof(name));
    }
    s_dirty = 1u;
}

/** @brief The last-scan summary from @p ctx's table. */
static void scan_summary(const struct scan_ctx *ctx)
{
    uint8_t i;

    scan_last_count = ctx->count;
    scan_have_best = 0u;
    for (i = 0u; i < ctx->count; i++) {
        if (!scan_have_best || ctx->out[i].rssi > scan_last_best.rssi) {
            scan_last_best = ctx->out[i];
            scan_have_best = 1u;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* BLOCKING SCAN                                                             */
/*---------------------------------------------------------------------------*/

int tiku_ble_adv_scan(tiku_ble_adv_report_t *out, uint8_t max, uint16_t ms)
{
    return tiku_ble_adv_scan_filter(out, max, ms, (const char *)0);
}

int tiku_ble_adv_scan_filter(tiku_ble_adv_report_t *out, uint8_t max,
                             uint16_t ms, const char *prefix)
{
    struct scan_ctx   ctx;
    size_t            plen;
    tiku_clock_time_t end;

    if (out == (tiku_ble_adv_report_t *)0 || max == 0u) {
        return -1;
    }
    /* Like the Nordic arbiter: an idle radio or a beacon admits a scan; a
     * running observer or a link does not. */
    if (s_observing || s_scanning ||
        tiku_ble_adv_owner() == TIKU_BLE_ADV_OWNER_CONN ||
        adv_radio_up() != 0) {
        return -1;
    }
    ctx.out = out;
    ctx.max = max;
    ctx.count = 0u;
    ctx.prefix = prefix;
    plen = (prefix != (const char *)0) ? strlen(prefix) : 0u;
    ctx.plen = (plen > TIKU_BLE_ADV_NAME_CAP)
                   ? (uint8_t)(TIKU_BLE_ADV_NAME_CAP + 1u)   /* matches none */
                   : (uint8_t)plen;

    /* Passive, the window filling the interval: every advertiser in range
     * within the time, as the Nordic scan hears them. */
    tiku_bt_set_adv_hook(adv_report);
    s_sink = &ctx;
    s_scanning = 1u;
    if (tiku_bt_scan_start(0u, 100u, 100u) != 0) {
        s_scanning = 0u;
        s_sink = (struct scan_ctx *)0;
        return -1;
    }
    end = (tiku_clock_time_t)(tiku_clock_time() +
          ((tiku_clock_time_t)ms * TIKU_CLOCK_SECOND) / 1000u);
    while (TIKU_CLOCK_LT(tiku_clock_time(), end)) {
        tiku_watchdog_kick();
        tiku_bt_poll();
        tiku_common_delay_ms(5u);
    }
    (void)tiku_bt_scan_stop();
    tiku_bt_poll();                             /* reports already queued */
    s_scanning = 0u;
    s_sink = (struct scan_ctx *)0;
    scan_summary(&ctx);
    return (int)ctx.count;
}

/*---------------------------------------------------------------------------*/
/* BACKGROUND OBSERVER                                                       */
/*---------------------------------------------------------------------------*/
/*
 * The stack's runner takes the controller's reports while the scan runs
 * and the hook fills the table; a callback timer every 2 ticks refreshes
 * the summary, calls the scan-notify hook when the table changed, and stops
 * the observer at its deadline.
 */

void tiku_ble_adv_set_scan_notify(void (*fn)(void))
{
    scan_notify_fn = fn;
}

static void observe_tick_cb(void *ptr)
{
    (void)ptr;
    if (!s_observing) {
        return;                     /* stopped between arm and dispatch    */
    }
    if (s_dirty) {
        s_dirty = 0u;
        scan_summary(&bg_ctx);
        if (scan_notify_fn != (void (*)(void))0) {
            scan_notify_fn();
        }
    }
    if (!observe_forever &&
        TIKU_CLOCK_LT(observe_deadline, tiku_clock_time())) {
        tiku_ble_adv_observe_stop();
        return;
    }
    tiku_timer_reset(&observe_timer);
}

int tiku_ble_adv_observe_start(uint16_t secs)
{
    if (s_observing || s_scanning ||
        tiku_ble_adv_owner() == TIKU_BLE_ADV_OWNER_CONN ||
        adv_radio_up() != 0) {
        return -1;
    }
    bg_ctx.out = bg_reports;
    bg_ctx.max = (uint8_t)OBSERVE_MAX_REPORTS;
    bg_ctx.count = 0u;
    bg_ctx.prefix = (const char *)0;
    bg_ctx.plen = 0u;
    scan_last_count = 0u;
    scan_have_best = 0u;
    s_dirty = 0u;
    tiku_bt_set_adv_hook(adv_report);
    s_sink = &bg_ctx;
    /* Half the time listening: the observer runs for minutes. */
    if (tiku_bt_scan_start(0u, 100u, 50u) != 0) {
        s_sink = (struct scan_ctx *)0;
        return -1;
    }
    s_observing = 1u;
    observe_forever = (uint8_t)(secs == 0u);
    observe_deadline = (tiku_clock_time_t)(tiku_clock_time() +
                       (tiku_clock_time_t)secs * TIKU_CLOCK_SECOND);
    tiku_timer_set_callback(&observe_timer, 2u, observe_tick_cb, (void *)0);
    return 0;
}

void tiku_ble_adv_observe_stop(void)
{
    if (!s_observing) {
        return;
    }
    tiku_timer_stop(&observe_timer);
    if (tiku_bt_is_ready()) {
        (void)tiku_bt_scan_stop();
        tiku_bt_poll();                         /* reports already queued */
    }
    s_observing = 0u;
    s_sink = (struct scan_ctx *)0;
    if (s_dirty || bg_ctx.count != scan_last_count) {
        s_dirty = 0u;
        scan_summary(&bg_ctx);
        if (scan_notify_fn != (void (*)(void))0) {
            scan_notify_fn();
        }
    }
}

int tiku_ble_adv_observing(void)
{
    return s_observing;
}

uint8_t tiku_ble_adv_observe_get(uint8_t idx, tiku_ble_adv_report_t *out)
{
    if (out == (tiku_ble_adv_report_t *)0 || idx >= bg_ctx.count) {
        return 0u;
    }
    *out = bg_reports[idx];
    return 1u;
}

uint8_t tiku_ble_adv_last_scan_count(void)
{
    return scan_last_count;
}

const tiku_ble_adv_report_t *tiku_ble_adv_last_scan_best(void)
{
    return scan_have_best ? &scan_last_best : (const tiku_ble_adv_report_t *)0;
}

#endif /* TIKU_BLE_ADV_PRESENT && TIKU_BT_HOST */
