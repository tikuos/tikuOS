/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_host.h - M33-side BLE host: the ATT/GATT server, fed L2CAP frames
 *                   by the FLPR controller across the shared-page mailbox.
 *
 * Phase B of the from-scratch BLE stack (kintsugi/radio.md): the FLPR is now
 * a pure link-layer CONTROLLER -- it advertises, holds the connection, runs
 * LL control (incl. the Phase A CONNECTION_UPDATE / CHANNEL_MAP follow), and
 * forwards raw L2CAP frames to/from this core.  Everything ABOVE L2CAP --
 * the NUS GATT database, ATT MTU/discovery/CCCD, write handling and Handle
 * Value Notifications -- lives here on the M33, where it has room to grow
 * (general GATT, long read/write, SMP) without the coprocessor's 16 KB carve.
 *
 * Frames are complete L2CAP PDUs: [len:2][CID:2][payload].  ATT is CID
 * 0x0004.  NUS payloads fit one data PDU, so no fragmentation yet (Phase C).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_HOST_H_
#define TIKU_BLE_HOST_H_

#include <stdint.h>

/** @brief Reset per-connection host state (ATT server, CCCD, RX buffer). */
void tiku_ble_host_reset(void);

/**
 * @brief Process one incoming L2CAP frame from the controller.
 *
 * Runs the ATT/GATT (NUS) server on the frame ([len][CID][payload]).  A NUS
 * RX write is buffered for tiku_ble_host_nus_recv().
 *
 * @param l2cap    incoming L2CAP frame.
 * @param len      its length in bytes.
 * @param out      buffer for a response L2CAP frame (may be produced).
 * @param out_cap  capacity of @p out.
 * @return response frame length written to @p out, or 0 if none.
 */
uint16_t tiku_ble_host_rx(const uint8_t *l2cap, uint16_t len,
                          uint8_t *out, uint16_t out_cap);

/**
 * @brief Pop NUS RX bytes the client wrote (the byte pipe).
 * @return bytes copied into @p buf (0 if none pending).
 */
uint16_t tiku_ble_host_nus_recv(uint8_t *buf, uint16_t cap);

/** @brief 1 once the client enabled TX notifications (wrote the CCCD). */
int tiku_ble_host_subscribed(void);

/**
 * @brief Build a NUS TX Handle Value Notification as an L2CAP frame.
 * @return frame length written to @p out, or 0 if not subscribed / bad args.
 */
uint16_t tiku_ble_host_nus_notify(const uint8_t *data, uint16_t len,
                                  uint8_t *out, uint16_t out_cap);

#endif /* TIKU_BLE_HOST_H_ */
