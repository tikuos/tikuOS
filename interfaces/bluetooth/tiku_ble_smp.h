/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_smp.h - LE Secure Connections crypto: AES-CMAC and f4/f5/f6/g2.
 *
 * The SMP key functions (Core Spec Vol 3, Part H, 2.2) over the CRACEN AES, and
 * a self-test.  The pairing engine that uses them is tiku_ble_smp_pair.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_SMP_H_
#define TIKU_BLE_SMP_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @brief AES-CMAC (RFC 4493) over the CRACEN AES-ECB.
 * @return 0 on success.
 */
int tiku_ble_smp_aes_cmac(const uint8_t key[16], const uint8_t *msg,
                          size_t len, uint8_t mac[16]);

/*
 * All f4/f5/f6/g2 inputs and the f4/f5/f6 outputs are in SMP wire order
 * (little-endian), as they appear on the L2CAP channel.  Internally each
 * function byte-swaps to the big-endian order the CMAC core operates on and
 * swaps the result back, so callers never see the endianness flip (Core Spec
 * Vol 3, Part H, 2.2.5-7); the self-test checks them against the spec's
 * sample data.
 */

/**
 * @brief SMP f4 confirm-value function: AES-CMAC_X(U || V || Z).
 * @param u,v 32-byte public-key X coordinates; @param x 16-byte nonce;
 * @param z 1 byte; @param out 16-byte confirm value.
 */
void tiku_ble_smp_f4(const uint8_t u[32], const uint8_t v[32],
                     const uint8_t x[16], uint8_t z, uint8_t out[16]);

/**
 * @brief SMP f5: derive MacKey (16) and LTK (16) from the DHKey.
 * @param w 32-byte DHKey; @param n1,n2 16-byte nonces;
 * @param a1t,a2t address types (1 = random, 0 = public);
 * @param a1,a2 6-byte device addresses; @param mackey,ltk 16-byte outputs.
 */
void tiku_ble_smp_f5(const uint8_t w[32], const uint8_t n1[16],
                     const uint8_t n2[16], uint8_t a1t, const uint8_t a1[6],
                     uint8_t a2t, const uint8_t a2[6],
                     uint8_t mackey[16], uint8_t ltk[16]);

/**
 * @brief SMP f6 check-value function:
 *        AES-CMAC_W(N1 || N2 || R || IOcap || A1 || A2).
 * @param iocap 3 bytes in wire order [io_cap, oob, authreq].
 */
void tiku_ble_smp_f6(const uint8_t w[16], const uint8_t n1[16],
                     const uint8_t n2[16], const uint8_t r[16],
                     const uint8_t iocap[3], uint8_t a1t, const uint8_t a1[6],
                     uint8_t a2t, const uint8_t a2[6], uint8_t out[16]);

/**
 * @brief SMP g2: the numeric-comparison value function.
 *
 * AES-CMAC_X(U || V || Y) keyed by X (Na); the caller takes the returned
 * 32-bit value modulo one million for the six digits both peers display.
 * @return the low 32 bits of the CMAC (spec: g2 mod 2^32).
 */
uint32_t tiku_ble_smp_g2(const uint8_t u[32], const uint8_t v[32],
                         const uint8_t x[16], const uint8_t y[16]);

/**
 * @brief Crypto self-test: AES-CMAC RFC-4493 KAT, f4/f5/f6 and g2 spec KATs,
 *        and a P-256 ECDH round-trip.
 * @return bitmask of passes: bit0 CMAC KAT, bit1 ECDH match, bit2 f4/f5/f6
 *         Core-spec KATs, bit3 g2 KAT (15 == all pass).
 */
int tiku_ble_smp_selftest(void);

#endif /* TIKU_BLE_SMP_H_ */
