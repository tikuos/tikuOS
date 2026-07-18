/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_host.h - M33-side BLE host: L2CAP (fragmentation/recombination) +
 *                   the ATT/GATT server, fed L2CAP FRAGMENTS by the FLPR
 *                   controller across the shared-page mailbox.
 *
 * Phase B split the peripheral into controller (FLPR) + host (M33).  Phase C
 * makes the host speak REAL L2CAP: a BLE data PDU carries at most ~27 bytes,
 * so an L2CAP PDU larger than that (any ATT payload past the 23-byte default
 * MTU) is fragmented across several data PDUs, tagged by the LL header's
 * LLID -- 0b10 (start) / 0b01 (continuation).  The controller forwards each
 * fragment with its LLID; this host RECOMBINES them into a whole L2CAP PDU
 * before running ATT, and FRAGMENTS its own responses/notifications back.
 * That unlocks payloads bigger than one PDU (a longer NUS message, a larger
 * MTU) -- the foundation general GATT (Phase D) needs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_HOST_H_
#define TIKU_BLE_HOST_H_

#include <stdint.h>

/** ATT MTU this host offers (and its L2CAP recombination bound). */
#define TIKU_BLE_HOST_MTU    64u

/** @brief Reset per-connection host state (ATT, CCCD, recomb + TX buffers). */
void tiku_ble_host_reset(void);

/**
 * @brief Feed one incoming L2CAP fragment from the controller.
 *
 * @param frag  fragment bytes (a slice of the L2CAP PDU).
 * @param len   fragment length.
 * @param llid  2 = start of an L2CAP PDU, 1 = continuation.
 *
 * Recombines; when a whole L2CAP PDU has arrived it runs the ATT/GATT server
 * and queues any response for tiku_ble_host_next_tx().  A NUS RX write is
 * buffered for tiku_ble_host_nus_recv().
 * @return 1 if a complete L2CAP PDU was processed this call, else 0.
 */
int tiku_ble_host_rx(const uint8_t *frag, uint16_t len, uint8_t llid);

/**
 * @brief Dole out the next TX fragment of the queued response/notification.
 * @param out      buffer for the fragment.
 * @param out_cap  capacity of @p out.
 * @param llid     out: 2 for the first fragment, 1 for continuations.
 * @return fragment length, or 0 when nothing is pending.
 */
uint16_t tiku_ble_host_next_tx(uint8_t *out, uint16_t out_cap, uint8_t *llid);

/**
 * @brief Pop NUS RX bytes the client wrote (the byte pipe, recombined).
 * @return bytes copied into @p buf (0 if none pending).
 */
uint16_t tiku_ble_host_nus_recv(uint8_t *buf, uint16_t cap);

/**
 * @brief Queue NUS TX bytes as a Handle Value Notification (fragmented on TX).
 * @return 0 on success, -2 if a TX PDU is still draining (retry), -1 if not
 *         subscribed / bad args.
 */
int tiku_ble_host_nus_notify(const uint8_t *data, uint16_t len);

/** @brief 1 once the client enabled TX notifications (wrote the CCCD). */
int tiku_ble_host_subscribed(void);

#endif /* TIKU_BLE_HOST_H_ */
