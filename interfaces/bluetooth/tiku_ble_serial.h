/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_serial.h - driver-agnostic "serial port over BLE" facade
 *
 * A deliberately small abstraction: advertise as a connectable peripheral that
 * exposes a byte pipe (the well-known Nordic UART Service GATT layout), tell me
 * when a central is connected + subscribed, and let me push/pull bytes.  It is
 * the wireless twin of a UART -- exactly what a high-level consumer (the BASIC
 * BLE words, a user app) wants, without touching HCI / L2CAP / ATT.
 *
 * This is intentionally NOT the full Bluetooth Core-Spec stack in tiku_bt.h
 * (HCI/GATT/GAP/SMP); it sits ABOVE a radio backend and can be implemented on
 * top of either.  Today the only backend is the EM9305 host stack on the
 * Apollo510 Blue (arch/ambiq/tiku_ble_uart); a future CYW43 backend would layer
 * the same facade over tiku_bt.h's GATT server + a NUS-style service.  Callers
 * stay portable: they see this facade, never the concrete radio.
 *
 * Capability: TIKU_BLE_SERIAL_PRESENT is 1 when the build compiled in a backend.
 * The build maps a concrete driver enable to the generic TIKU_HAS_BLE capability
 * (see the Makefile), so consumers gate on the capability, not on any one chip.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_SERIAL_H_
#define TIKU_BLE_SERIAL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when a BLE-serial backend is compiled in, 0 otherwise.  TIKU_HAS_BLE is the
 * generic capability the build sets from whichever radio driver is enabled; the
 * concrete-driver fallback keeps this correct even if a caller pulls the header
 * in without the Makefile's -D (e.g. a unit build). */
#if (defined(TIKU_HAS_BLE) && (TIKU_HAS_BLE + 0)) ||                          \
    (defined(TIKU_DRV_BLE_EM9305_ENABLE) && (TIKU_DRV_BLE_EM9305_ENABLE + 0))
#define TIKU_BLE_SERIAL_PRESENT 1
#else
#define TIKU_BLE_SERIAL_PRESENT 0
#endif

/**
 * @brief Is a BLE-serial radio backend present in this build?
 * @return 1 if the facade is backed by a real radio, 0 if it is a stub.
 */
int tiku_ble_serial_available(void);

/**
 * @brief Start connectable advertising as a BLE serial peripheral.
 *
 * A central (phone app, tikuble.py, the mac GUI) can then connect and subscribe;
 * after that, tiku_ble_serial_ready() turns true and send/recv carry data.
 *
 * @param name  Advertised local name (NULL -> a sensible default).
 * @return 0 on success, negative on failure (no radio / HCI error).
 */
int tiku_ble_serial_start(const char *name);

/** @brief Stop advertising and drop any link. Idempotent. */
void tiku_ble_serial_stop(void);

/**
 * @brief 1 when a central is connected AND subscribed to notifications, i.e.
 *        it is safe to send.  Polls the stack as a side effect, so a poll loop
 *        on this keeps the link serviced.
 *
 * A brief settle window is enforced after a fresh subscribe (notifications sent
 * while the central is still arming its subscription are silently dropped), so
 * the first send after this returns true actually lands.
 */
int tiku_ble_serial_ready(void);

/**
 * @brief Pump the stack once (drain events/RX, service TX acks).  Cheap and
 *        non-blocking; ready()/recv()/send() all call it, so an app that polls
 *        any of them need not call this explicitly.
 */
void tiku_ble_serial_service(void);

/**
 * @brief Send @p len bytes to the connected central (flow-controlled).
 *
 * Blocks only as long as controller credits require, bounded by a stall
 * deadline so a dead link cannot wedge the caller.
 *
 * @return Number of bytes accepted (== @p len) or negative if not connected.
 */
int tiku_ble_serial_send(const uint8_t *data, uint16_t len);

/**
 * @brief Are there received bytes waiting to be read?  Polls the stack, so this
 *        is the cheap, allocation-free predicate to gate a recv() poll loop on.
 * @return 1 if at least one byte is waiting, 0 otherwise.
 */
int tiku_ble_serial_rx_ready(void);

/**
 * @brief Pop up to @p cap bytes the central has written to us.
 * @return Number of bytes copied (0 if none waiting).
 */
int tiku_ble_serial_recv(uint8_t *buf, uint16_t cap);

/**
 * @brief Start a non-connectable beacon advertising @p name.
 * @return 0 on success, negative on failure.
 */
int tiku_ble_serial_beacon(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_BLE_SERIAL_H_ */
