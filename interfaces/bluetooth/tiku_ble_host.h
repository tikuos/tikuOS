/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_host.h - M33-side BLE host: L2CAP plus the ATT/GATT server.
 *
 * Without Data Length Extension a BLE data PDU carries 27 bytes, so a larger
 * L2CAP PDU is fragmented and tagged by the LL header's LLID.  This host
 * recombines inbound fragments before running ATT and fragments its replies.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_HOST_H_
#define TIKU_BLE_HOST_H_

#include <stdint.h>

/** ATT MTU this host offers. */
#define TIKU_BLE_HOST_MTU    64u

/** @brief Reset per-connection state (ATT, CCCD, recomb + TX buffers, SMP). */
void tiku_ble_host_reset(void);

/**
 * @brief Feed one incoming L2CAP fragment from the controller.
 *
 * Recombines; a whole L2CAP PDU goes to the ATT/GATT server, the signalling
 * channel or SMP, and any response is queued for tiku_ble_host_next_tx().
 * A NUS RX write is buffered for tiku_ble_host_nus_recv().
 *
 * @param frag  fragment bytes (a slice of the L2CAP PDU).
 * @param len   fragment length.
 * @param llid  2 = start of an L2CAP PDU, 1 = continuation.
 * @return 1 if a complete L2CAP PDU was processed this call, else 0.
 */
int tiku_ble_host_rx(const uint8_t *frag, uint16_t len, uint8_t llid);

/**
 * @brief Copy out the next TX fragment of the queued response/notification.
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

/**
 * @brief Send an L2CAP Connection Parameter Update Request.
 *
 * The peripheral-initiated way to ask the central for new parameters; it
 * replies with a Response and, if it accepts, an update the controller follows.
 * Queued for TX like any L2CAP PDU, and fits one data PDU.
 *
 * @return 0 queued, -2 if a TX PDU is still draining (retry).
 */
int tiku_ble_host_request_conn_param(uint16_t interval_min,
                                     uint16_t interval_max,
                                     uint16_t latency, uint16_t timeout);

/** @brief 1 once the central's Connection Parameter Update Response arrived
 *         (accepted); 0 not yet, -1 rejected. */
int tiku_ble_host_conn_param_result(void);

/**
 * @brief Data Length Extension: set the TX fragment size to the negotiated max
 *        LL payload, so a whole L2CAP PDU rides one LL PDU.  Clamped to >= the
 *        27-byte legacy minimum.
 */
void tiku_ble_host_set_frag_max(uint8_t n);

/**
 * @brief Largest L2CAP PDU received whole in one LL PDU this connection; a
 *        value above 31 means a payload over 27 bytes arrived unfragmented.
 */
uint16_t tiku_ble_host_max_single_frag(void);

/* --- SMP pairing (responder, L2CAP CID 0x0006) -------------------------- */

/**
 * @brief Arm the SMP responder for this connection.
 *
 * Incoming CID 0x0006 PDUs then drive the LE-SC pairing;
 * tiku_ble_host_smp_pump() wraps the engine's replies in L2CAP and queues them.
 *
 * @param inita  initiator (central) address A (6 B, little-endian).
 * @param at     A's address type (1 = random, 0 = public).
 * @param adva   advertiser (local) address B (6 B).
 * @param bt     B's address type.
 * @note Call once per connection, after it is up.
 */
void tiku_ble_host_smp_start(const uint8_t inita[6], uint8_t at,
                             const uint8_t adva[6], uint8_t bt);

/**
 * @brief Stage the next queued SMP PDU into the TX path if it is free.
 * @return 1 if a PDU was staged, 0 if none pending / TX busy.
 * @note Call after each TX drain until it returns 0: the responder's steps
 *       that send two PDUs (e.g. Public Key + Confirm) need it.
 */
int tiku_ble_host_smp_pump(void);

/** @brief SMP state: 0 idle, 1 pairing, 2 done (LTK ready), 3 failed. */
int tiku_ble_host_smp_state(void);

/** @brief Copy the derived LTK (valid only when state == 2). @return 0/-1. */
int tiku_ble_host_smp_ltk(uint8_t ltk[16]);

#endif /* TIKU_BLE_HOST_H_ */
