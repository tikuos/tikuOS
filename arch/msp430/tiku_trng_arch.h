/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - MSP430 software entropy source (no hardware TRNG)
 *
 * The MSP430FR5xxx/6xxx parts carry no dedicated true-random-number
 * generator (unlike the RP2350 ROSC-TRNG or the Apollo CryptoCell-312).
 * This driver synthesises one in software from two physical noise
 * sources on the die, so the same one-blocking-read API and the same
 * kit-side binding (tikukits/net/tls/.../tiku_kits_crypto_tls_config.h,
 * which backs TIKU_KITS_CRYPTO_TLS_RNG_FILL) work identically to the
 * hardware-TRNG ports:
 *
 *   1. Oscillator-ratio jitter -- MCLK (DCO, ~8 MHz) and ACLK (the
 *      32.768 kHz XT1 crystal) are independent, unlocked oscillators.
 *      Counting MCLK loop iterations across exactly one ACLK tick gives
 *      a ratio whose low bits walk unpredictably with the two sources'
 *      cycle-to-cycle jitter and relative drift.  This is the reliable
 *      source and gates the health check.
 *   2. ADC thermal noise -- the low bits of repeated 12-bit reads of the
 *      internal temperature sensor (a high-impedance, noisy channel).
 *
 * Both are pooled over many rounds and conditioned with SHA-256, so the
 * low per-round entropy still yields a full-entropy 256-bit block.  A
 * dead timer or a stuck jitter source fails the health test and the read
 * returns TIKU_TRNG_ERR_TIMEOUT (buffer unmodified) -- the TLS layer then
 * fails closed rather than handshaking on weak entropy.  Callers that
 * need many bytes should seed a DRBG once from this (as the HTTPS layer
 * does): collection is deliberately slow (~10 ms per 32-byte block).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MSP430_TRNG_ARCH_H_
#define TIKU_MSP430_TRNG_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Return codes for the TRNG driver (shared with the HW-TRNG ports).
 *
 * TIKU_TRNG_OK            — success.
 * TIKU_TRNG_ERR_INVALID   — NULL pointer or zero-length buffer.
 * TIKU_TRNG_ERR_TIMEOUT   — a health test failed (dead timer / stuck
 *                           jitter source); the output buffer is left
 *                           unmodified.
 * TIKU_TRNG_ERR_NOT_READY — tiku_trng_arch_init() was not called (unused
 *                           here: reads auto-init).
 */
#define TIKU_TRNG_OK             0
#define TIKU_TRNG_ERR_INVALID   -1
#define TIKU_TRNG_ERR_TIMEOUT   -2
#define TIKU_TRNG_ERR_NOT_READY -3

/**
 * @brief One-time init: configure the ADC for the thermal-noise source.
 *        Idempotent; auto-called on the first read.
 */
void tiku_trng_arch_init(void);

/**
 * @brief Fill @p buf with @p len cryptographically-conditioned random
 *        bytes.  Fails closed (returns TIKU_TRNG_ERR_TIMEOUT, buffer
 *        partially written) if a health test trips.
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

/**
 * @brief Blocking read of one 32-bit random word.
 */
int tiku_trng_arch_read_u32(uint32_t *out);

#endif /* TIKU_MSP430_TRNG_ARCH_H_ */
