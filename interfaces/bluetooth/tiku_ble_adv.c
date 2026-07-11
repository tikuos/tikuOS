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

/* One 3-channel burst; runs as a timer callback in the arming process's
 * context, then re-arms drift-free.  The per-burst HF clock request that
 * makes a post-sleep burst decodable lives in the arch send path. */
static void adv_burst_cb(void *ptr)
{
    (void)ptr;
    tiku_radio_arch_adv_send(adv_pdu, adv_pdu_len);
    adv_burst_count++;
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
    /* F4: when the coprocessor firmware is alive, the whole beacon runs
     * THERE -- no kernel timer is armed, so the M33 never wakes for a
     * burst.  The link config was just programmed by init (radio still
     * secure at that point); the arch call flips RADIO+UARTE21 to the
     * FLPR and ships the PDU. */
    if (tiku_flpr_arch_alive() &&
        tiku_flpr_arch_beacon(adv_pdu, adv_pdu_len, interval_ms) == 0) {
        /* Retune from an M33-timer beacon: the timer MUST die with the
         * hand-off -- its next burst would touch RADIO/UARTE21 through
         * the secure alias after the offload flipped them NonSecure
         * (precise bus fault, BFAR=UARTE21_S; measured). */
        tiku_timer_stop(&adv_timer);
        adv_offloaded = 1u;
        adv_on = 1u;
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

    /* First burst now (a beacon should be instantly visible), then the
     * timer paces the rest; set_callback re-sets an already-active timer. */
    tiku_radio_arch_adv_send(adv_pdu, adv_pdu_len);
    adv_burst_count++;
    tiku_timer_set_callback(&adv_timer, ticks, adv_burst_cb, (void *)0);
    adv_on = 1u;
    return 0;
}

void tiku_ble_adv_stop(void)
{
    if (adv_on) {
#if (TIKU_FLPR_ENABLE + 0)
        if (adv_offloaded) {
            tiku_flpr_arch_beacon_stop();
            adv_offloaded = 0u;
        }
#endif
        tiku_timer_stop(&adv_timer);
        tiku_radio_arch_constlat_hold(0);
        adv_on = 0u;
        adv_name[0] = '\0';
        adv_data_len = 0u;
        adv_interval_ms = 0u;
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

/* Per-CRC-OK-packet: dedup by AdvA, keep strongest RSSI + first name. */
static void scan_cb(const uint8_t *buf, uint8_t len, int8_t rssi, void *ud)
{
    struct scan_ctx *ctx = (struct scan_ctx *)ud;
    uint8_t type = (uint8_t)(buf[0] & 0x0Fu);
    const uint8_t *adva = &buf[3];      /* erratum-49 S1 slot at buf[2]     */
    tiku_ble_adv_report_t *slot = (tiku_ble_adv_report_t *)0;
    uint8_t i;

    /* Only PDUs whose payload begins with AdvA: ADV_IND(0),
     * ADV_DIRECT_IND(1), ADV_NONCONN_IND(2), SCAN_RSP(4), ADV_SCAN_IND(6);
     * and a plausible length. */
    if (len < 6u ||
        !(type == 0u || type == 1u || type == 2u || type == 4u ||
          type == 6u)) {
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
    /* AD structures follow AdvA except for ADV_DIRECT_IND (TargetA). */
    if (slot->name[0] == '\0' && type != 1u && len > 6u) {
        scan_parse_name(&buf[9], (uint8_t)(len - 6u), slot->name);
    }
}

int tiku_ble_adv_scan(tiku_ble_adv_report_t *out, uint8_t max, uint16_t ms)
{
    struct scan_ctx ctx;
    uint8_t i;

    if (out == (tiku_ble_adv_report_t *)0 || max == 0u) {
        return -1;
    }
    ctx.out = out;
    ctx.max = max;
    ctx.count = 0u;

#if (TIKU_FLPR_ENABLE + 0)
    if (adv_offloaded) {
        return -1;                  /* radio owned by the coprocessor      */
    }
#endif
    adv_radio_init_once();
    tiku_radio_arch_scan(scan_cb, &ctx, ms, (uint32_t *)0, (uint32_t *)0);

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

uint8_t tiku_ble_adv_last_scan_count(void)
{
    return scan_last_count;
}

const tiku_ble_adv_report_t *tiku_ble_adv_last_scan_best(void)
{
    return scan_have_best ? &scan_last_best : (const tiku_ble_adv_report_t *)0;
}

#endif /* TIKU_BLE_ADV_PRESENT */
