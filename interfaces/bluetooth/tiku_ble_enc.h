/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_enc.h - shared parameters for the LL data-encryption demo.
 *
 * Once encryption startup has agreed the session key and IV, one AES-CCM payload
 * is sent and MIC-verified.  This header only fixes what both ends must agree on:
 * the demo plaintext, the 1-byte AAD and the BLE nonce layout.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_ENC_H_
#define TIKU_BLE_ENC_H_

#include <stdint.h>

/* A fixed plaintext both ends compile in, so the two-board test proves the
 * ciphertext decrypts to exactly this (not just "MIC happened to verify"). */
#define TIKU_BLE_ENC_DEMO_PT \
    { 'T','I','K','U','-','L','L','-','C','C','M','-','D','E','M','O' }
#define TIKU_BLE_ENC_DEMO_PT_LEN 16u
#define TIKU_BLE_ENC_DEMO_AAD    0x00u      /* 1-byte AAD (LL-header stand-in) */

/**
 * @brief Build the 13-byte BLE AES-CCM nonce = packetCounter(5) || IV(8).
 *        packetCounter is the 39-bit counter (LSO first) with the direction
 *        bit in bit 7 of octet 4 (1 = central->peripheral).
 */
static inline void tiku_ble_enc_nonce(uint8_t nonce[13], uint32_t ctr,
                                      uint8_t dir, const uint8_t iv[8])
{
    int i;
    nonce[0] = (uint8_t)ctr;
    nonce[1] = (uint8_t)(ctr >> 8);
    nonce[2] = (uint8_t)(ctr >> 16);
    nonce[3] = (uint8_t)(ctr >> 24);
    nonce[4] = (uint8_t)(dir ? 0x80u : 0x00u);   /* counter bits 32-38 = 0   */
    for (i = 0; i < 8; i++) {
        nonce[5 + i] = iv[i];
    }
}

#endif /* TIKU_BLE_ENC_H_ */
