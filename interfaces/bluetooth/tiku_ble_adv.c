/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_adv.c - BLE broadcaster/observer facade, nRF54L15 backend.
 *
 * Backend-specific implementation of interfaces/bluetooth/tiku_ble_adv.h
 * on the on-die 2.4 GHz RADIO (arch/nordic/tiku_radio_arch).  Compiled only
 * when the build maps a broadcast-capable radio to TIKU_HAS_BLE_ADV (see
 * the Makefile's nordic section); consumers gate on TIKU_BLE_ADV_PRESENT.
 *
 * Concurrency model (see the header): the background beacon is a CALLBACK
 * software timer dispatched by the cooperative scheduler in the arming
 * process's context.  Nothing here preempts anything -- a blocking scan
 * simply delays the next burst -- so radio ownership needs no locking.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/bluetooth/tiku_ble_adv.h>

#if TIKU_BLE_ADV_PRESENT

#include <arch/nordic/tiku_radio_arch.h>
#if (TIKU_FLPR_ENABLE + 0)
#include <arch/nordic/tiku_flpr_arch.h>    /* F4: beacon offload           */
#endif
#include <arch/nordic/tiku_timer_arch.h>   /* TIKU_CLOCK_ARCH_SECOND        */
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/cpu/tiku_common.h>        /* tiku_common_unique_id         */
#include <string.h>

/* BLE advInterval legal range for legacy non-connectable advertising. */
#define BLE_ADV_INTERVAL_MIN_MS   100u
#define BLE_ADV_INTERVAL_MAX_MS   10240u
#define BLE_ADV_INTERVAL_DFLT_MS  1000u

/*---------------------------------------------------------------------------*/
/* Beacon state                                                              */
/*---------------------------------------------------------------------------*/

static uint8_t  adv_pdu[48] __attribute__((aligned(4))); /* radio DMA source */
static uint8_t  adv_pdu_len;
static struct tiku_timer adv_timer;
static char     adv_name[TIKU_BLE_ADV_NAME_CAP + 1];
static uint8_t  adv_data[TIKU_BLE_ADV_DATA_CAP];
static uint8_t  adv_data_len;       /* telemetry payload, for readback     */
static uint16_t adv_interval_ms;
static uint8_t  adv_on;
static uint32_t adv_burst_count;
static uint8_t  radio_inited;
static uint8_t  adv_offloaded;      /* beacon runs on the FLPR (F4)        */

/* Last-scan summary for /sys/radio observability. */
static uint8_t               scan_last_count;
static tiku_ble_adv_report_t scan_last_best;
static uint8_t               scan_have_best;

/* Ownership arbiter (R7): one radio, one owner; deny, never queue.
 * Replaces the scattered adv_offloaded checks as THE gate -- the
 * adv_offloaded flag survives below purely as FLPR mechanics. */
static uint8_t radio_owner;             /* tiku_ble_adv_owner_t            */

tiku_ble_adv_owner_t tiku_ble_adv_owner(void)
{
    return (tiku_ble_adv_owner_t)radio_owner;
}

const char *tiku_ble_adv_owner_str(void)
{
    static const char *names[6] = {
        "idle", "beacon", "beacon-flpr", "scan", "observe",
        "beacon+observe",
    };
    return names[radio_owner];
}

static void adv_radio_init_once(void)
{
    if (!radio_inited) {
        tiku_radio_arch_init();
        radio_inited = 1u;
    }
}

/* Random static device address (Core spec: two MSBs = 11), derived from the
 * FICR device ID so every board is distinct yet stable across boots. */
static void adv_random_addr(uint8_t addr[6])
{
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;
}

/* One 3-channel burst.  In combined mode (R7.5) the beacon time-divides
 * the radio with a running observer: borrow it (disarm RX), burst, hand
 * it back (re-arm RX, ring intact).  The ~1.3 ms RX blackout per beacon
 * interval is the whole cost -- 0.13% at a 1 s cadence.  Cooperative
 * dispatch means this callback and the observer's service tick never
 * overlap, so the borrow needs no locking. */
static void beacon_burst(void)
{
    if (radio_owner == TIKU_BLE_ADV_OWNER_BEACON_OBSERVE) {
        tiku_radio_arch_scan_pause();
        tiku_radio_arch_adv_send(adv_pdu, adv_pdu_len);
        tiku_radio_arch_scan_resume();
    } else {
        tiku_radio_arch_adv_send(adv_pdu, adv_pdu_len);
    }
    adv_burst_count++;
}

/* Timer callback in the arming process's context; re-arms drift-free.
 * The per-burst HF clock request that makes a post-sleep burst decodable
 * lives in the arch send path. */
static void adv_burst_cb(void *ptr)
{
    (void)ptr;
    beacon_burst();
    tiku_timer_reset(&adv_timer);
}

int tiku_ble_adv_available(void)
{
    return 1;
}

int tiku_ble_adv_beacon(const char *name, uint16_t interval_ms)
{
    return tiku_ble_adv_beacon_data(name, interval_ms, (const uint8_t *)0,
                                    0u);
}

int tiku_ble_adv_beacon_data(const char *name, uint16_t interval_ms,
                             const uint8_t *data, uint8_t data_len)
{
    uint8_t ad[31], addr[6];
    uint8_t adlen = 0u, nlen;
    tiku_clock_time_t ticks;
    /* Arbiter (R7.5): a beacon and the background observer time-divide
     * one radio, so starting a beacon while OBSERVE is allowed and
     * transitions to the combined owner.  obs_active also forces the
     * M33-timer path below -- the FLPR beacon drives the radio
     * NonSecure continuously and cannot coexist with an M33 observer. */
    uint8_t obs_active = (radio_owner == TIKU_BLE_ADV_OWNER_OBSERVE ||
                          radio_owner == TIKU_BLE_ADV_OWNER_BEACON_OBSERVE);

    /* A blocking scan owns the CPU synchronously (nothing else runs), so
     * SCAN is denied defensively. */
    if (radio_owner == TIKU_BLE_ADV_OWNER_SCAN) {
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
     * by the telemetry payload -- the `ADC -> beacon -> any phone` path. */
    ad[adlen++] = 0x02u; ad[adlen++] = 0x01u; ad[adlen++] = 0x06u;
    ad[adlen++] = (uint8_t)(1u + nlen);
    ad[adlen++] = 0x09u;
    memcpy(&ad[adlen], name, nlen); adlen = (uint8_t)(adlen + nlen);
    ad[adlen++] = (uint8_t)(3u + data_len);
    ad[adlen++] = 0xFFu; ad[adlen++] = 'T';
    ad[adlen++] = 'K';
    if (data_len) {
        memcpy(&ad[adlen], data, data_len);
        adlen = (uint8_t)(adlen + data_len);
    }

    adv_radio_init_once();
    adv_random_addr(addr);
    adv_pdu_len = tiku_radio_arch_adv_build(adv_pdu, addr, ad, adlen);

    memcpy(adv_name, name, nlen);
    adv_name[nlen] = '\0';
    if (data_len) {
        memcpy(adv_data, data, data_len);
    }
    adv_data_len = data_len;
    adv_interval_ms = interval_ms;

    ticks = (tiku_clock_time_t)(((uint32_t)interval_ms *
                                 (uint32_t)TIKU_CLOCK_SECOND) / 1000u);
    if (ticks == 0u) {
        ticks = 1u;
    }

    /* Erratum-20 discipline: hold Constant Latency across the whole beacon
     * session so the tickless sleeps between bursts happen in that mode.
     * (The decodability of post-sleep bursts itself comes from the per-
     * burst HF clock kick in the arch send path.) */
    tiku_radio_arch_constlat_hold(1);

#if (TIKU_FLPR_ENABLE + 0)
    /* F4: when the coprocessor firmware is alive AND no M33 observer is
     * running, the whole beacon runs THERE -- no kernel timer is armed,
     * so the M33 never wakes for a burst.  The link config was just
     * programmed by init (radio still secure at that point); the arch
     * call flips RADIO+UARTE21 to the FLPR and ships the PDU.  Skipped
     * under a live observer: the offload would seize the radio NonSecure
     * out from under the M33 RX engine. */
    if (!obs_active && tiku_flpr_arch_alive() &&
        tiku_flpr_arch_beacon(adv_pdu, adv_pdu_len, interval_ms) == 0) {
        /* Retune from an M33-timer beacon: the timer MUST die with the
         * hand-off -- its next burst would touch RADIO/UARTE21 through
         * the secure alias after the offload flipped them NonSecure
         * (precise bus fault, BFAR=UARTE21_S; measured). */
        tiku_timer_stop(&adv_timer);
        adv_offloaded = 1u;
        adv_on = 1u;
        radio_owner = TIKU_BLE_ADV_OWNER_BEACON_FLPR;
        return 0;
    }
    /* Retune fell back to the M33 path while offloaded (coprocessor died
     * or refused): reclaim the peripherals for the secure alias first --
     * beacon_stop flips RADIO+UARTE21 back even if the FLPR never
     * answers. */
    if (adv_offloaded) {
        tiku_flpr_arch_beacon_stop();
        adv_offloaded = 0u;
    }
#endif

    /* Combined owner FIRST, so beacon_burst() below takes the borrow
     * path when an observer is live.  First burst now (a beacon should
     * be instantly visible), then the timer paces the rest; set_callback
     * re-sets an already-active timer. */
    radio_owner = obs_active ? TIKU_BLE_ADV_OWNER_BEACON_OBSERVE
                             : TIKU_BLE_ADV_OWNER_BEACON;
    beacon_burst();
    tiku_timer_set_callback(&adv_timer, ticks, adv_burst_cb, (void *)0);
    adv_on = 1u;
    return 0;
}

void tiku_ble_adv_stop(void)
{
    if (adv_on) {
        uint8_t was_combined =
            (uint8_t)(radio_owner == TIKU_BLE_ADV_OWNER_BEACON_OBSERVE);
#if (TIKU_FLPR_ENABLE + 0)
        if (adv_offloaded) {
            tiku_flpr_arch_beacon_stop();
            adv_offloaded = 0u;
        }
#endif
        tiku_timer_stop(&adv_timer);
        adv_on = 0u;
        adv_name[0] = '\0';
        adv_data_len = 0u;
        adv_interval_ms = 0u;
        if (was_combined) {
            /* R7.5: hand the radio back to the still-running observer --
             * its RX is armed (the last burst resumed it) and its timer
             * is live; keep the CONSTLAT hold, it is still active. */
            radio_owner = TIKU_BLE_ADV_OWNER_OBSERVE;
        } else {
            tiku_radio_arch_constlat_hold(0);
            radio_owner = TIKU_BLE_ADV_OWNER_IDLE;
        }
    }
}

int tiku_ble_adv_active(void)
{
    return adv_on ? 1 : 0;
}

const char *tiku_ble_adv_name(void)
{
    return adv_name;
}

uint16_t tiku_ble_adv_interval_ms(void)
{
    return adv_interval_ms;
}

uint8_t tiku_ble_adv_data(const uint8_t **out)
{
    if (out != (const uint8_t **)0) {
        *out = adv_data;
    }
    return adv_data_len;
}

int tiku_ble_adv_set_txpower(int8_t dbm)
{
#if (TIKU_FLPR_ENABLE + 0)
    if (adv_offloaded) {
        /* The RADIO answers only on its NonSecure alias while the FLPR
         * owns it -- a TXPOWER write through the secure alias is a
         * precise bus fault (the F4 retune lesson).  Reclaim, set, then
         * re-arm through beacon_data so the proven offload/fallback
         * interlocks (timer kill, SPU flip-back on a dead coprocessor)
         * all apply.  Copies because beacon_data writes the same
         * statics it reads. */
        char    nm[TIKU_BLE_ADV_NAME_CAP + 1];
        uint8_t d[TIKU_BLE_ADV_DATA_CAP];
        uint8_t dl;

        tiku_flpr_arch_beacon_stop();
        if (tiku_radio_arch_set_txpower(dbm) != 0) {
            /* Invalid step: restore the offloaded beacon unchanged. */
            (void)tiku_flpr_arch_beacon(adv_pdu, adv_pdu_len,
                                        adv_interval_ms);
            return -1;
        }
        memcpy(nm, adv_name, sizeof(nm));
        dl = adv_data_len;
        memcpy(d, adv_data, sizeof(d));
        return tiku_ble_adv_beacon_data(nm, adv_interval_ms,
                                        dl ? d : (const uint8_t *)0, dl);
    }
#endif
    /* Idle or M33-timer beacon: the register write lands between bursts
     * and is latched at the next ramp-up.  Pre-init calls just store the
     * value; init applies it. */
    return tiku_radio_arch_set_txpower(dbm);
}

int8_t tiku_ble_adv_txpower(void)
{
    return tiku_radio_arch_txpower();
}

uint32_t tiku_ble_adv_bursts(void)
{
#if (TIKU_FLPR_ENABLE + 0)
    if (adv_offloaded) {
        return adv_burst_count + tiku_flpr_arch_beacon_bursts();
    }
#endif
    return adv_burst_count;
}

/*---------------------------------------------------------------------------*/
/* Observer (passive scan)                                                   */
/*---------------------------------------------------------------------------*/

struct scan_ctx {
    tiku_ble_adv_report_t *out;
    uint8_t max;
    uint8_t count;
    const char *prefix;                 /* insert-time name filter (or NULL) */
    uint8_t plen;
};

/* Extract the Local Name (complete 0x09 preferred over shortened 0x08)
 * from the AD structures. */
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

/* 'TK'-manufacturer-data fallback name, used only while a filter is
 * armed: a BlueZ host CANNOT put its Local Name in a legacy ADV payload
 * (instances append the name to the SCAN RESPONSE -- kernel
 * MGMT_ADV_FLAG_LOCAL_NAME semantics; hardware-measured: the host showed
 * up as a strong nameless ADV_SCAN_IND), so the reverse-nonce oracle
 * ships its nonce as ASCII after the 'TK' company id (0x4B54, our own
 * beacon marker) -- manufacturer data DOES ride in the ADV payload.
 * Restricted to the TK id so ambient vendor blobs (Apple beacons etc.)
 * can never masquerade as a name. */
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

/* Per-CRC-OK-packet: dedup by AdvA, keep strongest RSSI + first name.
 * With a prefix filter armed, the name gates SLOT ALLOCATION: in a busy
 * environment the small report table would otherwise fill with ambient
 * advertisers before the sought device is heard (the reason the TikuBench
 * reverse-nonce oracle needs this).  Nameless PDUs (incl. ADV_DIRECT_IND,
 * whose payload carries TargetA, not AD) are dropped while filtering. */
static void scan_cb(const uint8_t *buf, uint8_t len, int8_t rssi, void *ud)
{
    struct scan_ctx *ctx = (struct scan_ctx *)ud;
    uint8_t type = (uint8_t)(buf[0] & 0x0Fu);
    const uint8_t *adva = &buf[3];      /* erratum-49 S1 slot at buf[2]     */
    tiku_ble_adv_report_t *slot = (tiku_ble_adv_report_t *)0;
    char name[TIKU_BLE_ADV_NAME_CAP + 1];
    uint8_t i;

    /* Only PDUs whose payload begins with AdvA: ADV_IND(0),
     * ADV_DIRECT_IND(1), ADV_NONCONN_IND(2), SCAN_RSP(4), ADV_SCAN_IND(6);
     * and a plausible length. */
    if (len < 6u ||
        !(type == 0u || type == 1u || type == 2u || type == 4u ||
          type == 6u)) {
        return;
    }

    /* Parse the Local Name up front: the insert-time filter needs it, and
     * a later sighting may fill a name the first packet lacked.  AD
     * structures follow AdvA except for ADV_DIRECT_IND (TargetA). */
    name[0] = '\0';
    if (type != 1u && len > 6u) {
        scan_parse_name(&buf[9], (uint8_t)(len - 6u), name);
        /* Filter armed + no Local Name: accept the 'TK' manufacturer
         * ASCII as the name (see scan_parse_mfr_tk -- the only legacy
         * ADV slot a BlueZ oracle can actually reach). */
        if (name[0] == '\0' && ctx->plen != 0u) {
            scan_parse_mfr_tk(&buf[9], (uint8_t)(len - 6u), name);
        }
    }
    if (ctx->plen != 0u && strncmp(name, ctx->prefix, ctx->plen) != 0) {
        return;
    }

    for (i = 0u; i < ctx->count; i++) {
        if (memcmp(ctx->out[i].addr, adva, 6u) == 0) {
            slot = &ctx->out[i];
            break;
        }
    }
    if (slot == (tiku_ble_adv_report_t *)0) {
        if (ctx->count >= ctx->max) {
            return;
        }
        slot = &ctx->out[ctx->count++];
        memcpy(slot->addr, adva, 6u);
        slot->rssi = rssi;
        slot->adv_type = type;
        slot->name[0] = '\0';
    } else if (rssi > slot->rssi) {
        slot->rssi = rssi;
    }
    if (slot->name[0] == '\0' && name[0] != '\0') {
        memcpy(slot->name, name, sizeof(name));
    }
}

int tiku_ble_adv_scan(tiku_ble_adv_report_t *out, uint8_t max, uint16_t ms)
{
    return tiku_ble_adv_scan_filter(out, max, ms, (const char *)0);
}

int tiku_ble_adv_scan_filter(tiku_ble_adv_report_t *out, uint8_t max,
                             uint16_t ms, const char *prefix)
{
    struct scan_ctx ctx;
    size_t plen;
    uint8_t i;

    if (out == (tiku_ble_adv_report_t *)0 || max == 0u) {
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

    /* Arbiter: the engine is busy while observing, and the RADIO answers
     * only on the NS alias while FLPR-offloaded.  An M33-timer beacon
     * keeps its historical coexistence (cooperative scheduling: its
     * bursts queue behind this blocking call), so claim SCAN and restore
     * the prior owner afterwards. */
    if (radio_owner == TIKU_BLE_ADV_OWNER_OBSERVE ||
        radio_owner == TIKU_BLE_ADV_OWNER_BEACON_FLPR) {
        return -1;
    }
    {
        uint8_t prev_owner = radio_owner;
        radio_owner = TIKU_BLE_ADV_OWNER_SCAN;
        adv_radio_init_once();
        tiku_radio_arch_scan(scan_cb, &ctx, ms, (uint32_t *)0,
                             (uint32_t *)0);
        radio_owner = prev_owner;
    }

    scan_last_count = ctx.count;
    scan_have_best = 0u;
    for (i = 0u; i < ctx.count; i++) {
        if (!scan_have_best || out[i].rssi > scan_last_best.rssi) {
            scan_last_best = out[i];
            scan_have_best = 1u;
        }
    }
    return (int)ctx.count;
}

/*---------------------------------------------------------------------------*/
/* Background observer (R7)                                                  */
/*---------------------------------------------------------------------------*/
/*
 * The non-blocking half of the observer: the arch engine (IRQ + hardware
 * windows) runs while the shell stays interactive, and a CALLBACK kernel
 * timer drains the packet ring every 2 ticks into a persistent dedup
 * table.  Results are live in the last-scan summary (and therefore
 * `cat /sys/radio/scan`); every service pass that delivered packets
 * fires the scan-notify hook, which the VFS tree maps to
 * tiku_vfs_notify(/sys/radio/scan) -- `watch` and the rules engine ride
 * the namespace event bus from there.
 */

#define OBSERVE_MAX_REPORTS  12u
static tiku_ble_adv_report_t bg_reports[OBSERVE_MAX_REPORTS];
static struct scan_ctx       bg_ctx;
static struct tiku_timer     observe_timer;
static tiku_clock_time_t     observe_deadline;
static uint8_t               observe_forever;
static void                (*scan_notify_fn)(void);

void tiku_ble_adv_set_scan_notify(void (*fn)(void))
{
    scan_notify_fn = fn;
}

/* Refresh the /sys/radio-visible summary from the observer's table. */
static void observe_update_summary(void)
{
    uint8_t i;

    scan_last_count = bg_ctx.count;
    scan_have_best = 0u;
    for (i = 0u; i < bg_ctx.count; i++) {
        if (!scan_have_best || bg_reports[i].rssi > scan_last_best.rssi) {
            scan_last_best = bg_reports[i];
            scan_have_best = 1u;
        }
    }
}

static void observe_tick_cb(void *ptr)
{
    uint8_t n;

    (void)ptr;
    if (radio_owner != TIKU_BLE_ADV_OWNER_OBSERVE &&
        radio_owner != TIKU_BLE_ADV_OWNER_BEACON_OBSERVE) {
        return;                     /* stopped between arm and dispatch    */
    }
    n = tiku_radio_arch_scan_service(scan_cb, &bg_ctx);
    if (n != 0u) {
        observe_update_summary();
        if (scan_notify_fn != (void (*)(void))0) {
            scan_notify_fn();       /* namespace event: re-read for value  */
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
    uint8_t combined;

    /* R7.5: starting the observer while a (non-offloaded) beacon runs
     * time-divides the radio -> combined owner.  Deny FLPR-offloaded
     * beacon (radio NonSecure), a blocking scan, or an observer already
     * running. */
    if (radio_owner != TIKU_BLE_ADV_OWNER_IDLE &&
        radio_owner != TIKU_BLE_ADV_OWNER_BEACON) {
        return -1;
    }
    combined = (uint8_t)(radio_owner == TIKU_BLE_ADV_OWNER_BEACON);

    adv_radio_init_once();
    bg_ctx.out = bg_reports;
    bg_ctx.max = (uint8_t)OBSERVE_MAX_REPORTS;
    bg_ctx.count = 0u;
    bg_ctx.prefix = (const char *)0;
    bg_ctx.plen = 0u;
    scan_last_count = 0u;
    scan_have_best = 0u;
    /* Unified Constant Latency invariant (R7.5): held while ANY radio
     * owner is active, released only at IDLE.  A beacon already holds
     * it; this makes observe hold it too, so the combined session and
     * either single survivor all stay in CONSTLAT (erratum 20). */
    tiku_radio_arch_constlat_hold(1);
    /* Owner BEFORE arming, so a beacon burst dispatched right after
     * takes the borrow path (cooperative dispatch means it cannot
     * actually fire until this returns, but keep the states honest). */
    radio_owner = combined ? TIKU_BLE_ADV_OWNER_BEACON_OBSERVE
                           : TIKU_BLE_ADV_OWNER_OBSERVE;
    tiku_radio_arch_scan_start();
    observe_forever = (uint8_t)(secs == 0u);
    observe_deadline = (tiku_clock_time_t)(tiku_clock_time() +
                       (tiku_clock_time_t)secs * TIKU_CLOCK_SECOND);
    tiku_timer_set_callback(&observe_timer, 2u, observe_tick_cb, (void *)0);
    return 0;
}

void tiku_ble_adv_observe_stop(void)
{
    uint8_t was_combined;

    if (radio_owner != TIKU_BLE_ADV_OWNER_OBSERVE &&
        radio_owner != TIKU_BLE_ADV_OWNER_BEACON_OBSERVE) {
        return;
    }
    was_combined = (uint8_t)(radio_owner == TIKU_BLE_ADV_OWNER_BEACON_OBSERVE);
    tiku_timer_stop(&observe_timer);
    tiku_radio_arch_scan_stop();    /* disarm RX; constlat_exit suppressed
                                     * while held (below)                  */
    /* Teardown stragglers, then one final summary refresh + ring. */
    if (tiku_radio_arch_scan_service(scan_cb, &bg_ctx) != 0u ||
        bg_ctx.count != scan_last_count) {
        observe_update_summary();
        if (scan_notify_fn != (void (*)(void))0) {
            scan_notify_fn();
        }
    }
    if (was_combined) {
        /* The beacon survives and keeps the CONSTLAT hold; its timer is
         * still armed and reverts to the plain (non-borrow) burst path. */
        radio_owner = TIKU_BLE_ADV_OWNER_BEACON;
    } else {
        radio_owner = TIKU_BLE_ADV_OWNER_IDLE;
        tiku_radio_arch_constlat_hold(0);
    }
}

int tiku_ble_adv_observing(void)
{
    return (radio_owner == TIKU_BLE_ADV_OWNER_OBSERVE ||
            radio_owner == TIKU_BLE_ADV_OWNER_BEACON_OBSERVE) ? 1 : 0;
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

#endif /* TIKU_BLE_ADV_PRESENT */
