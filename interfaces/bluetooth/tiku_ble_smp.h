/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_smp.h - LE Secure Connections (SMP) pairing, Just Works.
 *
 * Phase E of the from-scratch BLE stack (kintsugi/radio.md).  The Security
 * Manager runs on L2CAP CID 0x0006: feature exchange -> P-256 public-key
 * exchange -> confirm/random -> f5 key derivation, ending with both peers
 * holding the same Long Term Key (LTK).  The crypto is standard BLE:
 *   - P-256 ECDH  (tikukits/crypto/p256) for the DHKey,
 *   - AES-CMAC    (RFC 4493, over tiku_crypto_arch_aes_ecb) for f4/f5/f6.
 * Role-aware: the central is initiator, the peripheral responder.  The module
 * is transport-agnostic -- it consumes/produces SMP PDUs; the caller moves
 * them over its own L2CAP path (M33 host mailbox, or the central engine).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_SMP_H_
#define TIKU_BLE_SMP_H_

#include <stdint.h>
#include <stddef.h>

/** @brief AES-CMAC (RFC 4493) over the CRACEN AES-ECB.  @return 0 on success. */
int tiku_ble_smp_aes_cmac(const uint8_t key[16], const uint8_t *msg,
                          size_t len, uint8_t mac[16]);

/**
 * @brief SMP f4 confirm-value function: AES-CMAC_X(U || V || Z).
 * @param u,v 32-byte public-key X coordinates; @param x 16-byte nonce;
 * @param z 1 byte; @param out 16-byte confirm value.
 */
void tiku_ble_smp_f4(const uint8_t u[32], const uint8_t v[32],
                     const uint8_t x[16], uint8_t z, uint8_t out[16]);

/**
 * @brief SMP f5: derive MacKey (16) and LTK (16) from the DHKey.
 * @param w 32-byte DHKey; @param n1,n2 16-byte nonces; @param a1,a2 7-byte
 *        addresses (type||addr); @param mackey,ltk 16-byte outputs.
 */
void tiku_ble_smp_f5(const uint8_t w[32], const uint8_t n1[16],
                     const uint8_t n2[16], const uint8_t a1[7],
                     const uint8_t a2[7], uint8_t mackey[16], uint8_t ltk[16]);

/**
 * @brief SMP f6 check-value function:
 *        AES-CMAC_W(N1 || N2 || R || IOcap || A1 || A2).
 */
void tiku_ble_smp_f6(const uint8_t w[16], const uint8_t n1[16],
                     const uint8_t n2[16], const uint8_t r[16],
                     const uint8_t iocap[3], const uint8_t a1[7],
                     const uint8_t a2[7], uint8_t out[16]);

/**
 * @brief Crypto self-test: AES-CMAC RFC-4493 KAT + P-256 ECDH round-trip.
 * @return bitmask of passes: bit0 CMAC KAT, bit1 ECDH match (3 == all pass).
 */
int tiku_ble_smp_selftest(void);

#endif /* TIKU_BLE_SMP_H_ */
