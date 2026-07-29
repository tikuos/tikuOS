/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_adv.h - driver-agnostic BLE broadcaster/observer facade.
 *
 * The connection-less sibling of tiku_ble_serial.h: advertise a non-connectable
 * beacon and passively scan with RSSI, without touching radio registers.  The
 * beacon is a re-arming software timer, so a build stays microwatt-compatible.
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

/** Longest telemetry payload (manufacturer data after the 'TK' id). */
#define TIKU_BLE_ADV_DATA_CAP  18

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

/**
 * @brief Start (or retune) the beacon with a telemetry payload.
 *
 * As tiku_ble_adv_beacon(), with @p data appended to the manufacturer data, so
 * any observer reads it without connecting.  Calling again swaps the payload.
 * Payload has AD-budget priority; the name is truncated if both cannot fit.
 *
 * @param data      Payload bytes (NULL -> none).
 * @param data_len  Payload length (capped at TIKU_BLE_ADV_DATA_CAP).
 */
int tiku_ble_adv_beacon_data(const char *name, uint16_t interval_ms,
                             const uint8_t *data, uint8_t data_len);

/** @brief Stop the background beacon. Idempotent. */
void tiku_ble_adv_stop(void);

/** @brief 1 while the background beacon is armed. */
int tiku_ble_adv_active(void);

/** @brief Current beacon name ("" when off). */
const char *tiku_ble_adv_name(void);

/** @brief Current beacon interval in ms (0 when off). */
uint16_t tiku_ble_adv_interval_ms(void);

/**
 * @brief Current telemetry payload (0 when none/off).
 * @param out  Receives a pointer to the payload bytes (may be NULL).
 * @return Payload length.
 */
uint8_t tiku_ble_adv_data(const uint8_t **out);

/** @brief Total advertising bursts transmitted since boot. */
uint32_t tiku_ble_adv_bursts(void);

/**
 * @brief Set the beacon TX power in dBm (default +8, the strongest).
 *
 * Only the silicon's discrete steps are legal; anything else is rejected, never
 * rounded.  It takes effect from the next burst and is safe while a beacon runs
 * -- the facade reclaims the radio, applies it and re-arms any offload.
 *
 * @return 0 on success, negative if @p dbm is not a legal step.
 */
int tiku_ble_adv_set_txpower(int8_t dbm);

/** @brief Currently configured TX power in dBm. */
int8_t tiku_ble_adv_txpower(void);

/**
 * @brief Passive scan of the advertising channels (blocking, watchdog-safe).
 *
 * Deduplicates by address, keeping the strongest RSSI and the first non-empty
 * name.  A running background beacon is unaffected: its bursts are cooperative
 * timer callbacks and simply queue behind the scan.
 *
 * @param out  Report array.
 * @param max  Capacity of @p out.
 * @param ms   Scan duration in milliseconds (wall clock).
 * @return Number of distinct devices heard (<= @p max), negative if no
 *         backend.
 */
int tiku_ble_adv_scan(tiku_ble_adv_report_t *out, uint8_t max, uint16_t ms);

/**
 * @brief Passive scan keeping only advertisers whose name starts with @p prefix.
 *
 * The filter gates SLOT ALLOCATION, not display, so ambient advertisers cannot
 * fill the small report table before the sought device is heard.  Nameless
 * advertisements are dropped while armed; an empty prefix behaves like _scan().
 */
int tiku_ble_adv_scan_filter(tiku_ble_adv_report_t *out, uint8_t max,
                             uint16_t ms, const char *prefix);

/*---------------------------------------------------------------------------*/
/* Radio ownership + background observer (R7)                                */
/*---------------------------------------------------------------------------*/

/*
 * One radio, one owner: the beacon, a blocking scan, or the background
 * observer.  Claims are denied rather than queued, since a beacon and an
 * observer would interleave TX bursts into live RX windows.  The blocking scan
 * keeps its historical coexistence with a timer beacon, which scheduling
 * already serialises.
 */
typedef enum {
    TIKU_BLE_ADV_OWNER_IDLE = 0,
    TIKU_BLE_ADV_OWNER_BEACON,          /**< M33 timer beacon              */
    TIKU_BLE_ADV_OWNER_BEACON_FLPR,     /**< beacon offloaded to the FLPR  */
    TIKU_BLE_ADV_OWNER_SCAN,            /**< blocking scan in flight       */
    TIKU_BLE_ADV_OWNER_OBSERVE,         /**< background observer           */
    TIKU_BLE_ADV_OWNER_BEACON_OBSERVE,  /**< R7.5: beacon + observer,
                                         *   time-divided on one radio     */
    TIKU_BLE_ADV_OWNER_CONN,            /**< L6: FLPR serial connection     */
    TIKU_BLE_ADV_OWNER_154,             /**< N-track: 802.15.4 owns the RADIO*/
} tiku_ble_adv_owner_t;

/** @brief Current radio owner. */
tiku_ble_adv_owner_t tiku_ble_adv_owner(void);

/** @brief Owner as a short string ("idle"/"beacon"/"beacon-flpr"/...). */
const char *tiku_ble_adv_owner_str(void);

/**
 * @brief Claim the radio for a connection (L6 FLPR serial link).
 *
 * One radio, one owner (R7): the serial link drives RADIO NonSecure via the
 * FLPR for the whole connection, so it cannot time-divide with a beacon or
 * observer.  Claim succeeds only from IDLE.
 *
 * @return 0 claimed, -1 the radio is already owned.
 */
int tiku_ble_adv_conn_claim(void);

/** @brief Release a connection claim (back to IDLE). Idempotent. */
void tiku_ble_adv_conn_release(void);

/**
 * @brief Claim the radio for 802.15.4 (mode-switches the shared RADIO, so it
 * cannot coexist with a BLE beacon/observer/connection).  Claim only from
 * IDLE.  @return 0 claimed, -1 already owned.
 */
int tiku_ble_adv_154_claim(void);

/** @brief Release a 15.4 claim (back to IDLE). Idempotent. */
void tiku_ble_adv_154_release(void);

/**
 * @brief Start the background observer (non-blocking scan).
 *
 * The IRQ and hardware-window engine runs while the shell stays interactive; a
 * timer callback drains the packet ring into the live results and fires the
 * notify hook, so `watch /sys/radio/scan` and the rules engine see updates.
 *
 * @param secs  Auto-stop after this many seconds; 0 = until
 *              tiku_ble_adv_observe_stop().
 * @return 0 on success, negative if the radio is owned (beacon active).
 */
int tiku_ble_adv_observe_start(uint16_t secs);

/** @brief Stop the background observer.  Idempotent. */
void tiku_ble_adv_observe_stop(void);

/** @brief 1 while the background observer runs. */
int tiku_ble_adv_observing(void);

/**
 * @brief Copy the observer table's @p idx-th report (0-based).
 *
 * Live while observing; the table persists after observe stops, so
 * results remain queryable (BLESEEN$(i)) until the next observe/scan.
 *
 * @return 1 and fills @p out when idx < count; 0 otherwise.
 */
uint8_t tiku_ble_adv_observe_get(uint8_t idx, tiku_ble_adv_report_t *out);

/**
 * @brief Install the new-scan-data hook (called from timer-callback
 *        context whenever the observer delivered packets).  The VFS
 *        tree uses it to tiku_vfs_notify(/sys/radio/scan).
 */
void tiku_ble_adv_set_scan_notify(void (*fn)(void));

/** @brief Devices heard by the most recent scan (0 before any scan). */
uint8_t tiku_ble_adv_last_scan_count(void);

/** @brief Strongest device from the most recent scan (NULL before any). */
const tiku_ble_adv_report_t *tiku_ble_adv_last_scan_best(void);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_BLE_ADV_H_ */
