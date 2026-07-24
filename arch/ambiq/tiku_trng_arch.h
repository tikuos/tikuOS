/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - Ambiq Apollo4/5 True Random Number Generator HAL
 *
 * Wraps the Arm CryptoCell-312 TRNG block embedded in the Apollo CRYPTO
 * peripheral (apollo4l/4p/510 share the same CC312 RNG register map).  A
 * ring oscillator feeds a 192-bit EHR (entropy holding register) split
 * across six 32-bit EHR_DATA[0..5] words; an EHR_VALID status bit flips when
 * the block is full.  Health tests (autocorrelation / CRNGT / Von Neumann)
 * gate each collection; a failed test re-arms the source.
 *
 * Same one-blocking-read API and 192-bit RAM cache as the RP2350 TRNG HAL,
 * so the kit-side binding (tikukits/net/tls/.../tiku_kits_crypto_tls_config.h)
 * is identical -- read_bytes() backs TIKU_KITS_CRYPTO_TLS_RNG_FILL.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_TRNG_ARCH_H_
#define TIKU_AMBIQ_TRNG_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Return codes for the TRNG driver.
 *
 * TIKU_TRNG_OK            — success.
 * TIKU_TRNG_ERR_INVALID   — NULL pointer or zero-length buffer.
 * TIKU_TRNG_ERR_TIMEOUT   — the EHR never validated (or health tests kept
 *                           failing) within the spin / retry budget; the
 *                           output buffer is left unmodified.
 * TIKU_TRNG_ERR_NOT_READY — tiku_trng_arch_init() was not called.
 */
#define TIKU_TRNG_OK            0
#define TIKU_TRNG_ERR_INVALID  -1
#define TIKU_TRNG_ERR_TIMEOUT  -2
#define TIKU_TRNG_ERR_NOT_READY -3

/**
 * @brief One-time init: power up the CRYPTO (CryptoCell-312) domain.
 *        Idempotent; auto-called on the first read.
 */
void tiku_trng_arch_init(void);

/**
 * @brief Block until a 32-bit random word is available; return it.
 *
 * Fast path returns the next word from the 6-word software cache the
 * hardware already filled; slow path re-arms the ring-oscillator source and
 * spins on EHR_VALID (re-arming on a health-test failure) for a bounded
 * budget before giving up — see TIKU_TRNG_ERR_TIMEOUT.
 *
 * @param out  Where to store the random word. Must not be NULL.
 * @return TIKU_TRNG_OK or a negative error code.
 */
int tiku_trng_arch_read_u32(uint32_t *out);

/**
 * @brief Fill a byte buffer with `len` cryptographically random bytes.
 *
 * Calls read_u32() repeatedly; bytes are uniformly random in any order.
 * Returns TIKU_TRNG_ERR_TIMEOUT (buffer partially written) on hardware
 * failure so the TLS layer fails closed rather than using weak entropy.
 *
 * @param buf  Destination buffer.
 * @param len  Number of bytes to write.
 * @return TIKU_TRNG_OK or a negative error code.
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

#endif /* TIKU_AMBIQ_TRNG_ARCH_H_ */
