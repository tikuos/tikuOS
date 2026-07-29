/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wireless.h - board-independent wireless-interface API.
 *
 * The kernel declares the API and types here and one driver supplies them, so
 * consumers call tiku_wireless_* rather than a driver name.  A single radio is
 * assumed; concurrent radios would add a per-radio handle as the first argument.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_WIRELESS_H_
#define TIKU_WIRELESS_H_

#include <stdint.h>
#include "kernel/drivers/tiku_drv.h"
#include "kernel/process/tiku_process.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* Constants                                                                 */
/*---------------------------------------------------------------------------*/

/** Maximum scan results the interface caches between scans. */
#ifndef TIKU_WIRELESS_MAX_SCAN_RESULTS
#define TIKU_WIRELESS_MAX_SCAN_RESULTS 16U
#endif

/*---------------------------------------------------------------------------*/
/* Types                                                                     */
/*---------------------------------------------------------------------------*/

/** One discovered access point (deduplicated by BSSID). */
typedef struct {
    uint8_t  bssid[6];        /* 802.11 BSSID */
    uint8_t  ssid_len;        /* 0..32 */
    uint8_t  ssid[32];        /* not null-terminated */
    int16_t  rssi;            /* dBm, signed */
    uint8_t  channel;         /* 1..13 (2.4 GHz) */
    uint8_t  _pad;            /* explicit pad, keep size predictable */
} tiku_wireless_ap_t;

/** Link state. */
typedef enum {
    TIKU_WIRELESS_LINK_IDLE       = 0,
    TIKU_WIRELESS_LINK_CONNECTING = 1,
    TIKU_WIRELESS_LINK_JOINED     = 2,
    TIKU_WIRELESS_LINK_FAILED     = 3,
} tiku_wireless_link_t;

/** Snapshot of the wireless interface's state. */
typedef struct {
    uint8_t  up;              /* 1 after the radio is reachable */
    uint8_t  scan_in_progress;
    uint16_t scan_aps_found;  /* deduplicated count from last scan */
    uint8_t  mac[6];          /* MAC address of the local radio */
    uint32_t last_scan_ticks; /* duration of last completed scan
                               * in TIKU_CLOCK_SECOND-aligned ticks */
    uint32_t irq_count;       /* total GPIO IRQ deliveries on the
                               * chip's wake line (instrumentation
                               * for R.6 — proof IRQ wiring is live) */

    /* Phase 4.B — station-mode join state. */
    uint8_t  link_state;      /* tiku_wireless_link_t */
    uint8_t  joined_ssid_len;
    uint8_t  joined_ssid[32]; /* not null-terminated; len in joined_ssid_len */
    uint8_t  joined_bssid[6];
    uint32_t link_status_raw; /* last raw status code from the chip's
                               * WLC_E_LINK event (debugging) */

    /* RSSI of the joined AP in dBm, polled from the chip when the
     * link is up. 0 = not yet polled or link down. */
    int16_t  rssi_dbm;

    /* Duration of the last completed join attempt, in
     * TIKU_CLOCK_SECOND-aligned ticks. Mirrors last_scan_ticks. */
    uint32_t last_join_ticks;
} tiku_wireless_status_t;

/*---------------------------------------------------------------------------*/
/* Events                                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_WIRELESS_EVT_SCAN_START     (TIKU_EVENT_USER + 0U)
#define TIKU_WIRELESS_EVT_SCAN_COMPLETE  (TIKU_EVENT_USER + 1U)
#define TIKU_WIRELESS_EVT_AP_FOUND       (TIKU_EVENT_USER + 2U)
#define TIKU_WIRELESS_EVT_LINK_UP        (TIKU_EVENT_USER + 3U)
#define TIKU_WIRELESS_EVT_LINK_DOWN      (TIKU_EVENT_USER + 4U)
#define TIKU_WIRELESS_EVT_JOIN_START     (TIKU_EVENT_USER + 5U)
#define TIKU_WIRELESS_EVT_DISCONNECT     (TIKU_EVENT_USER + 6U)

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Trigger an active scan (non-blocking).
 *
 * Subscribers get an AP_FOUND event per unique access point and one
 * SCAN_COMPLETE when it ends.  Fails if the radio is not up, or if a scan is
 * already in flight or the runner's queue is full.
 */
int tiku_wireless_scan_start(void);

/**
 * @brief Copy the last scan's deduplicated results.
 *
 * @param out         Caller-supplied array, at least @p max_results
 *                    entries.
 * @param max_results Capacity of @p out.
 * @return Number of entries actually written (0 if no scan completed).
 */
uint8_t tiku_wireless_scan_results(tiku_wireless_ap_t *out,
                                   uint8_t max_results);

/**
 * @brief Snapshot interface state. Synchronous, no side effects.
 */
int tiku_wireless_status(tiku_wireless_status_t *out);

/** Auth flavors for tiku_wireless_connect_auth. */
typedef enum {
    TIKU_WIRELESS_AUTH_WPA2_PSK = 0,   /* default — IEEE 802.11i RSN  */
    TIKU_WIRELESS_AUTH_WPA3_SAE = 1,   /* simultaneous authentication */
} tiku_wireless_auth_t;

/**
 * @brief Join a WPA2-PSK network. Non-blocking — the runner does
 *        the IOCTL sequence on its next dispatch and waits for the
 *        chip's WLC_E_LINK event before transitioning state to
 *        JOINED (or FAILED). Caller observes via tiku_wireless_status.
 *
 * @param ssid  Network SSID (1..32 chars, null-terminated)
 * @param psk   WPA2 passphrase (8..63 chars, null-terminated)
 * @return TIKU_DRV_OK on enqueue; TIKU_DRV_ERR_INVALID on bad args
 *         or radio-not-up; TIKU_DRV_ERR_TIMEOUT if a join is already
 *         in flight.
 */
int tiku_wireless_connect(const char *ssid, const char *psk);

/**
 * @brief Join a WPA2-PSK or WPA3-SAE network. Same semantics as
 *        tiku_wireless_connect with explicit auth flavor.
 *
 * @param ssid Network SSID (1..32 chars, null-terminated)
 * @param psk  Passphrase (WPA2: 8..63; WPA3: 1..127 chars)
 * @param auth TIKU_WIRELESS_AUTH_WPA2_PSK or _WPA3_SAE
 */
int tiku_wireless_connect_auth(const char *ssid, const char *psk,
                               tiku_wireless_auth_t auth);

/**
 * @brief Tear down the current association. Non-blocking. Stored
 *        credentials (if any) are preserved — a subsequent reboot
 *        will still cold-boot-rejoin. Use tiku_wireless_forget()
 *        to also wipe the saved SSID/PSK.
 */
int tiku_wireless_disconnect(void);

/**
 * @brief Forget the persistent WPA credentials cached after the last join.
 *
 * Tears down any current association, clears the stored SSID and PSK, and stops
 * cold-boot rejoin on the next reboot.  Idempotent, so it is safe on a device
 * that has none.
 *
 * @return TIKU_DRV_OK on success.
 */
int tiku_wireless_forget(void);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_WIRELESS_H_ */
