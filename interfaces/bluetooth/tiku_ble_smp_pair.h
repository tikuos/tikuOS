/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_smp_pair.h - LE Secure Connections "Just Works" pairing engine.
 *
 * A transport-agnostic state machine that drives the SMP exchange on L2CAP
 * CID 0x0006 to a shared Long Term Key.  It consumes and produces raw SMP
 * PDUs (opcode || payload); the caller wraps them in L2CAP + moves them over
 * its own path (the M33 host mailbox as responder, the RADIO central engine
 * as initiator).  The crypto (P-256 ECDH, AES-CMAC, f4/f5/f6) lives in
 * tiku_ble_smp.{c,h}; this file is only the protocol.
 *
 * Flow (Core Spec Vol 3, Part H, 2.3.5.6 -- LE SC, Just Works / no MITM):
 *   I -> R  Pairing Request                R -> I  Pairing Response
 *   I -> R  Pairing Public Key (PKa)       R -> I  Pairing Public Key (PKb)
 *                                          R -> I  Pairing Confirm (Cb)
 *   I -> R  Pairing Random (Na)            R -> I  Pairing Random (Nb)
 *   I -> R  DHKey Check (Ea)               R -> I  DHKey Check (Eb)
 * Both ends derive (MacKey, LTK) = f5(DHKey, Na, Nb, A, B) and cross-check the
 * DHKey checks with f6; a match on both sides means matching LTKs.
 *
 * One pairing at a time (single connection); all state is static.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_SMP_PAIR_H_
#define TIKU_BLE_SMP_PAIR_H_

#include <stdint.h>

/** Local role in the pairing exchange. */
typedef enum {
    TIKU_BLE_SMP_ROLE_INITIATOR = 0,     /* central   */
    TIKU_BLE_SMP_ROLE_RESPONDER = 1      /* peripheral */
} tiku_ble_smp_role_t;

/** Engine state. */
typedef enum {
    TIKU_BLE_SMP_STATE_IDLE = 0,         /* not started                     */
    TIKU_BLE_SMP_STATE_PAIRING,          /* exchange in progress            */
    TIKU_BLE_SMP_STATE_DONE,             /* LTK derived, DHKey checks OK     */
    TIKU_BLE_SMP_STATE_FAILED            /* aborted / verification mismatch  */
} tiku_ble_smp_state_t;

/** Largest SMP PDU we emit/consume: Pairing Public Key = 1 + 64 = 65 bytes. */
#define TIKU_BLE_SMP_PDU_MAX  65u

/** @brief Clear all pairing state back to IDLE. */
void tiku_ble_smp_pair_reset(void);

/**
 * @brief Begin a pairing.  Generates the P-256 keypair + local nonce; the
 *        initiator also stages the first Pairing Request for next().
 * @param role  our role.
 * @param a,at  initiator (central) address A (6 B, little-endian) + type.
 * @param b,bt  responder (peripheral) address B (6 B) + type (1 = random).
 *        Both callers pass A then B (initiator-first), regardless of role.
 * @return 0 on success, -1 on crypto/entropy failure.
 */
int tiku_ble_smp_pair_start(tiku_ble_smp_role_t role,
                            const uint8_t a[6], uint8_t at,
                            const uint8_t b[6], uint8_t bt);

/**
 * @brief Feed one received SMP PDU (opcode || payload, no L2CAP header).
 *        Advances the state machine and may stage outgoing PDU(s) for next().
 * @return 1 if consumed, 0 if ignored (wrong state / bad length).
 */
int tiku_ble_smp_pair_feed(const uint8_t *pdu, uint16_t len);

/**
 * @brief Pop the next SMP PDU the engine wants to send.
 * @param out  buffer (>= TIKU_BLE_SMP_PDU_MAX).
 * @return PDU length, or 0 if nothing is queued.
 */
uint16_t tiku_ble_smp_pair_next(uint8_t *out, uint16_t cap);

/** @brief Current engine state. */
tiku_ble_smp_state_t tiku_ble_smp_pair_state(void);

/**
 * @brief Copy the derived Long Term Key (valid only in state DONE).
 * @return 0 and fills @p ltk when DONE, else -1.
 */
int tiku_ble_smp_pair_ltk(uint8_t ltk[16]);

#endif /* TIKU_BLE_SMP_PAIR_H_ */
