/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crypto_arch.h - nRF54L15 CRACEN CryptoMaster offload (hash first).
 *
 * Hardware acceleration behind the tikukits/crypto software APIs: the kit
 * entry points stay the contract, the software implementation stays the
 * always-present reference, and a RUNTIME mode selects the path:
 *
 *   mode 0 (auto) - try the CRACEN engine, fall back to software on any
 *                   hardware error (fail-open to correctness);
 *   mode 1 (sw)   - software only (the A/B switch for benchmarks and a
 *                   kill-switch if the engine misbehaves in the field).
 *
 * Counters expose which path actually ran, so the crypto-hw TikuBench
 * suite can assert "hardware really executed" instead of trusting the
 * mode knob.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_CRYPTO_ARCH_H_
#define TIKU_NORDIC_CRYPTO_ARCH_H_

#include <stdint.h>
#include <stddef.h>

#define TIKU_CRYPTO_HW_MODE_AUTO  0u
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
 * @brief One-shot SHA-256 of @p len bytes at @p msg into @p out[32].
 * @return 0 on success; negative if the engine errored (caller falls back).
 */
int tiku_crypto_arch_sha256(const void *msg, size_t len, uint8_t out[32]);

/**
 * @brief One-shot AES-GCM through the BA411 engine.
 *
 * @param decrypt    0 = encrypt, 1 = decrypt (tag is PRODUCED either way;
 *                   the caller compares on decrypt)
 * @param cfg_extra  extra config-word bits (bring-up knob; 0 in production)
 * @param out        needs align-4 headroom past @p in_sz (FIFO realign)
 * @return 0 ok; -2 unsupported shape (caller falls back to software)
 */
int tiku_crypto_arch_aes_gcm(int decrypt, uint32_t cfg_extra,
                             const uint8_t *key, size_t key_sz,
                             const uint8_t iv[12],
                             const uint8_t *aad, size_t aad_sz,
                             const uint8_t *in, size_t in_sz,
                             uint8_t *out, uint8_t tag[16]);

/** @brief Kit-safe AES-GCM (staged; no caller alignment/RRAM constraints). */
int tiku_crypto_arch_aes_gcm_kit(int decrypt,
                                 const uint8_t *key, size_t key_sz,
                                 const uint8_t iv[12],
                                 const uint8_t *aad, size_t aad_sz,
                                 const uint8_t *in, size_t in_sz,
                                 uint8_t *out, uint8_t tag[16]);

/*---------------------------------------------------------------------------*/
/* Bring-up probes (cryptoprobe shell command; not part of the kit contract) */
/*---------------------------------------------------------------------------*/

/** @brief Run the hash engine with an arbitrary config word (bring-up). */
int tiku_crypto_arch_hash_probe(uint32_t cfg, const void *msg, size_t len,
                                uint8_t *out, size_t outlen);

/** @brief DMA self-test: fetch -> bypass engine -> push (no crypto). */
int tiku_crypto_arch_bypass_probe(const void *msg, size_t len, uint8_t *out);

/** @brief DIRECT-mode DMA self-test (no descriptors at all). */
int tiku_crypto_arch_direct_probe(const void *msg, size_t len, uint8_t *out);

/** @brief Bring-up debug: last INTSTATRAW/STATUS, live SEEDVALID, stage. */
void tiku_crypto_arch_dbg(uint32_t *ints, uint32_t *status,
                          uint32_t *seedvalid, uint32_t *stage);

/** @brief Read a CRYPTMSTRHW fused-configuration word (idx 0..6). */
uint32_t tiku_crypto_arch_hwcfg(uint8_t idx);

#endif /* TIKU_NORDIC_CRYPTO_ARCH_H_ */
