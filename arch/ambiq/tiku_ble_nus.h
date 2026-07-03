/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_nus.h - Minimal connectable GATT peripheral (Nordic UART Service)
 *
 * Layers a tiny host stack on the EM9305 HCI transport (tiku_em9305): a polled
 * HCI event/ACL pump, LE connection handling, an L2CAP-LE + ATT server, and the
 * Nordic UART Service (NUS) -- the wireless-shell transport (M3). No Cordio, no
 * AmbiqSuite. Built only for the BLE config (TIKU_DRV_BLE_EM9305_ENABLE).
 *
 * Bring-up is staged; this header is the whole M3 API, implemented stage by
 * stage: connect (M3.1), ATT discovery (M3.2), NUS data + shell wiring (M3.3).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_NUS_H_
#define TIKU_BLE_NUS_H_

#include <stdint.h>

/** @brief Events returned by tiku_ble_nus_poll(). */
#define TIKU_BLE_EVT_NONE          0   /**< nothing pending                  */
#define TIKU_BLE_EVT_CONNECTED     1   /**< a central connected              */
#define TIKU_BLE_EVT_DISCONNECTED  2   /**< the link dropped                 */
#define TIKU_BLE_EVT_ATT           3   /**< an ATT request was served        */
#define TIKU_BLE_EVT_RX            4   /**< NUS RX bytes arrived (M3.3)       */

/**
 * @brief Reset the radio and begin CONNECTABLE advertising as @p name.
 *
 * Resets, HCI Reset, LE Set Advertising Parameters (ADV_IND, connectable),
 * Advertising Data (Flags + Complete Local Name), Advertising Enable.
 *
 * @param name  Advertised complete local name (NULL -> "tikuOS").
 * @return 0 on success (advertising enabled), negative on failure.
 */
int tiku_ble_nus_start(const char *name);

/**
 * @brief Pump the stack once (non-blocking): read at most one pending HCI
 *        packet and dispatch it (connection events, ATT requests, NUS writes).
 * @return One of TIKU_BLE_EVT_*.
 */
int tiku_ble_nus_poll(void);

/**
 * @brief Per-step diagnostics from the last tiku_ble_nus_start().
 *
 * Copies the result code and status byte of each setup command
 * (0=HCI Reset, 1=LE Event Mask, 2=Adv Params, 3=Adv Data, 4=Adv Enable) into
 * @p rc / @p st, up to @p cap entries.
 *
 * @return Number of steps attempted.
 */
uint8_t tiku_ble_nus_start_steps(int8_t *rc, uint8_t *st, uint8_t cap);

/**
 * @brief Copy the raw bytes of the most recent LE Meta event (diagnostics).
 * @return Number of bytes copied (0 if none seen yet).
 */
uint8_t tiku_ble_nus_last_meta(uint8_t *buf, uint8_t cap);

/** @brief Number of packets in the pump trace ring (diagnostics). */
uint8_t tiku_ble_nus_trace_count(void);

/**
 * @brief Copy trace entry @p i (0 = oldest) into @p buf.
 * @return Number of bytes copied (0 if @p i is out of range).
 */
uint8_t tiku_ble_nus_trace(uint8_t i, uint8_t *buf, uint8_t cap);

/** @brief Non-zero while a central is connected. */
int tiku_ble_nus_connected(void);

/** @brief Non-zero once the peer has enabled TX notifications (subscribed). */
int tiku_ble_nus_notify_enabled(void);

/* --- shell io-backend hooks (build a tiku_shell_io_t from these) --- */

/** @brief Pop one byte the peer wrote to NUS RX (-1 if none). */
int tiku_ble_nus_getc(void);

/** @brief Non-zero if NUS RX bytes are waiting. */
uint8_t tiku_ble_nus_rx_ready(void);

/** @brief Queue one output byte for NUS TX (auto-flushed when a notification
 *         fills); pair with tiku_ble_nus_flush() to push the tail. */
void tiku_ble_nus_putc(char c);

/** @brief Send any buffered TX bytes as a notification now. */
void tiku_ble_nus_flush(void);

/** @brief Number of TX bytes still buffered (for paced draining). */
uint16_t tiku_ble_nus_tx_pending(void);

/** @brief Snapshot of the current (or most recent) LE connection. */
typedef struct {
    uint16_t handle;       /**< HCI connection handle (0xFFFF if none)      */
    uint8_t  role;         /**< 0 = we are peripheral (expected)            */
    uint8_t  peer_type;    /**< peer address type                           */
    uint8_t  peer[6];      /**< peer address, LSB first (as HCI delivers)   */
} tiku_ble_nus_conn_t;

/** @brief Pointer to the internal connection snapshot (never NULL). */
const tiku_ble_nus_conn_t *tiku_ble_nus_conn_info(void);

/** @brief Stop advertising / drop any link (best-effort). */
void tiku_ble_nus_stop(void);

#endif /* TIKU_BLE_NUS_H_ */
