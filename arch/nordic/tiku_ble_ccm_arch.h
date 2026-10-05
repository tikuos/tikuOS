/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_ccm_arch.h - CCM00 hardware AES-CCM for the BLE link layer.
 *
 * The RADIO-companion CCM engine, driven memory to memory with a software
 * START: scatter/gather MVDMA in and out, BLE packet format with a
 * header-masked 1-byte AAD and an M=4 MIC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_CCM_ARCH_H_
#define TIKU_BLE_CCM_ARCH_H_

#include <stdint.h>

/**
 * @brief One-shot CCM00 crypt of a BLE LL payload (memory to memory).
 *
 * Encrypt: @p out gets ciphertext + 4-byte MIC (@p len + 4 bytes).
 * Decrypt: @p in is ciphertext + MIC (@p len = payload length without the
 * MIC); @p out gets the plaintext; returns the MIC verdict.
 *
 * @param decrypt 0 = encrypt, 1 = decrypt (FastDecryption).
 * @param sk      16-byte session key (wire order, as tiku_ble_enc uses).
 * @param nonce   13-byte BLE nonce (tiku_ble_enc_nonce layout).
 * @param aad     the 1-byte AAD (first LL header octet; CCM00 masks it
 *                with ADATAMASK internally, so pass the raw header byte).
 * @param in      input payload (plaintext, or ciphertext+MIC).
 * @param len     payload length in bytes (1..27+DLE, excl. MIC).
 * @param out     output buffer (len + 4 for encrypt, len for decrypt).
 * @return 0 = OK (decrypt: MIC verified), -1 = MIC check failed,
 *         -2 - ERRORSTATUS on an engine error or timeout.
 */
int tiku_ble_ccm_arch_crypt(int decrypt, const uint8_t sk[16],
                            const uint8_t nonce[13], uint8_t aad,
                            const uint8_t *in, uint8_t len, uint8_t *out);

/**
 * @brief Known-answer test of CCM00 against the software CCM*
 *        (tiku_crypto_arch_aes_ccm_star) with the Core-spec sample key/IV.
 * @return bitmask: 1 = encrypt matches software, 2 = decrypt round-trips
 *         with MIC OK, 4 = tampered MIC rejected.  7 = all pass.
 */
int tiku_ble_ccm_arch_selftest(void);

#endif /* TIKU_BLE_CCM_ARCH_H_ */
