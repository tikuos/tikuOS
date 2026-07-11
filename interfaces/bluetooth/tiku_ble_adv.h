/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_adv.h - driver-agnostic BLE broadcaster/observer facade
 *
 * The connection-less sibling of tiku_ble_serial.h: advertise a beacon
 * (non-connectable ADV_NONCONN_IND with Flags + Complete Local Name + a
 * 2-byte 'TK' manufacturer marker) and observe the advertising channels
 * (passive scan with RSSI).  It is what a high-level consumer (the BASIC
 * BLEBEACON/BLESCAN$ words, /sys/radio, shell commands) wants from a
 * broadcast radio, without touching the radio registers.
 *
 * tiku_ble_serial.h is connection-shaped (advertise -> connect -> byte
 * pipe) and needs a link-layer-capable backend (today the EM9305 host
 * stack).  This facade is broadcast-shaped and is backed by the nRF54L15
 * on-die RADIO (arch/nordic/tiku_radio_arch); the two capabilities are
 * independent: a build may have either, both, or neither.
 *
 * The background beacon is a kernel software timer in CALLBACK mode: each
 * expiry transmits one 3-channel burst (~1.3 ms) and re-arms drift-free
 * with tiku_timer_reset().  Between bursts the system idles normally
 * (tickless WFI; the session holds Constant Latency per nRF54L15 erratum
 * 20, and the arch send path issues the per-burst HF clock kick that makes
 * a post-sleep burst decodable), which is what keeps a beacon compatible
 * with microwatt operation.
 * The callback runs in the context of the process that started the beacon,
 * dispatched by the cooperative scheduler -- so it never preempts a
 * blocking scan; radio ownership needs no locking.
 *
 * Capability: TIKU_BLE_ADV_PRESENT is 1 when the build compiled in a
 * backend.  The Makefile maps the concrete radio to the generic
 * TIKU_HAS_BLE_ADV capability (nRF54L15 today); consumers gate on the
 * capability, never on a chip.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_ADV_H_
#define TIKU_BLE_ADV_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if (defined(TIKU_HAS_BLE_ADV) && (TIKU_HAS_BLE_ADV + 0))
#define TIKU_BLE_ADV_PRESENT 1
#else
#define TIKU_BLE_ADV_PRESENT 0
#endif

/** Longest advertised name the 31-byte AD payload can carry here. */
#define TIKU_BLE_ADV_NAME_CAP  21

/** One observed advertiser (see tiku_ble_adv_scan()). */
typedef struct {
    uint8_t addr[6];                    /**< AdvA, little-endian (on-air)   */
    int8_t  rssi;                       /**< Strongest RSSI seen, dBm       */
    uint8_t adv_type;                   /**< PDU type (0/1/2/4/6)           */
    char    name[TIKU_BLE_ADV_NAME_CAP + 1]; /**< Local name, "" if absent  */
} tiku_ble_adv_report_t;

/** @brief 1 when a broadcast radio backend is present in this build. */
int tiku_ble_adv_available(void);

/**
 * @brief Start (or retune) the background beacon.
 *
 * Builds the PDU (Flags + name + 'TK' manufacturer data, random static
 * address derived from the device ID) and transmits one 3-channel burst
 * per interval from a kernel timer while the system otherwise sleeps.
 *
 * @param name         Advertised local name (NULL/"" -> "tikuOS").
 * @param interval_ms  Burst interval; 0 -> 1000 ms. Clamped to [100, 10240]
 *                     (BLE advInterval legal range for this PDU type).
 * @return 0 on success, negative if no backend.
 */
int tiku_ble_adv_beacon(const char *name, uint16_t interval_ms);

/** @brief Stop the background beacon. Idempotent. */
void tiku_ble_adv_stop(void);

/** @brief 1 while the background beacon is armed. */
int tiku_ble_adv_active(void);

/** @brief Current beacon name ("" when off). */
const char *tiku_ble_adv_name(void);

/** @brief Current beacon interval in ms (0 when off). */
uint16_t tiku_ble_adv_interval_ms(void);

/** @brief Total advertising bursts transmitted since boot. */
uint32_t tiku_ble_adv_bursts(void);

/**
 * @brief Passive scan of the advertising channels (blocking, watchdog-safe).
 *
 * Deduplicates by address, keeps the strongest RSSI per device and the
 * first non-empty name.  A running background beacon is unaffected: its
 * bursts are timer callbacks dispatched cooperatively, so they simply
 * queue behind the scan.
 *
 * @param out  Report array.
 * @param max  Capacity of @p out.
 * @param ms   Scan duration in milliseconds (wall clock).
 * @return Number of distinct devices heard (<= @p max), negative if no
 *         backend.
 */
int tiku_ble_adv_scan(tiku_ble_adv_report_t *out, uint8_t max, uint16_t ms);

/** @brief Devices heard by the most recent scan (0 before any scan). */
uint8_t tiku_ble_adv_last_scan_count(void);

/** @brief Strongest device from the most recent scan (NULL before any). */
const tiku_ble_adv_report_t *tiku_ble_adv_last_scan_best(void);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_BLE_ADV_H_ */
