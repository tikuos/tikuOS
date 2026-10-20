/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crypto_arch.h - P4 SHA and AES engines behind the software crypto APIs.
 *
 * The same contract as the Nordic port: auto tries the engine and falls back
 * to software on any error, sw forces the reference path, counters show which
 * path ran.  The ECDSA engine needs silicon revision 3.0; this one is 1.3.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_CRYPTO_ARCH_H_
#define TIKU_P4_CRYPTO_ARCH_H_
#include <stdint.h>
#include <stddef.h>

/** @brief Engine mode: try the hardware, fall back to software on error. */
#define TIKU_CRYPTO_HW_MODE_AUTO  0u
/** @brief Engine mode: software only. */
#define TIKU_CRYPTO_HW_MODE_SW    1u

/** @brief Get the runtime engine mode (AUTO=0, SW=1). */
uint8_t tiku_crypto_hw_mode(void);
/** @brief Set the runtime engine mode (AUTO=0, SW=1). */
void    tiku_crypto_hw_mode_set(uint8_t mode);
/** @brief Path counters: ops served by hardware, by software, hw errors. */
void    tiku_crypto_hw_counters(uint16_t *hw_ops, uint16_t *sw_ops,
                                uint16_t *hw_errs);
/** @brief Kit software paths call this when they serve an op. */
void    tiku_crypto_hw_count_sw(void);
/**
 * @brief SHA-256 of @p len bytes at @p msg into @p out[32], one 64-byte block
 *        at a time with interrupts masked per block.
 * @return 0 on success, -2 for a NULL pointer, -1 if the engine stays busy
 */
int tiku_crypto_arch_sha256(const void *msg, size_t len, uint8_t out[32]);
/**
 * @brief One AES block in the engine's typical (non-DMA) mode.
 * @return 0 on success, -2 for a key size other than 16 or 32 bytes, -1 if
 *         the engine stays busy
 */
int tiku_crypto_arch_aes_ecb(int decrypt, const uint8_t *key, size_t key_sz,
                             const uint8_t in[16], uint8_t out[16]);
/**
 * @brief AES-GCM with a 12-byte IV: CTR blocks on the engine, GHASH in
 *        software.  The tag is produced on encrypt and decrypt alike; the
 *        caller compares on decrypt.
 * @return 0 ok, -2 for a bad key size or NULL buffer, -1 on an engine error
 */
int tiku_crypto_arch_aes_gcm_kit(int decrypt,
                                 const uint8_t *key, size_t key_sz,
                                 const uint8_t iv[12],
                                 const uint8_t *aad, size_t aad_sz,
                                 const uint8_t *in, size_t in_sz,
                                 uint8_t *out, uint8_t tag[16]);
/** @brief tiku_crypto_arch_aes_gcm_kit() under the Nordic probe's signature;
 *         @p cfg_extra must be 0.  @return as aes_gcm_kit, or -2 */
int tiku_crypto_arch_aes_gcm(int decrypt, uint32_t cfg_extra,
                             const uint8_t *key, size_t key_sz,
                             const uint8_t iv[12],
                             const uint8_t *aad, size_t aad_sz,
                             const uint8_t *in, size_t in_sz,
                             uint8_t *out, uint8_t tag[16]);

#endif /* TIKU_P4_CRYPTO_ARCH_H_ */
