/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - P4 ECO2 RNG with internal SAR entropy.
 *
 * The RNG mixes noise from an internal SAR ADC channel; no board pin is
 * routed or sampled.  Calls return TIKU_TRNG_OK or a TIKU_TRNG_ERR_* code.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_TRNG_ARCH_H_
#define TIKU_P4_TRNG_ARCH_H_
#include <stddef.h>
#include <stdint.h>
#define TIKU_TRNG_OK 0
#define TIKU_TRNG_ERR_INVALID -1
#define TIKU_TRNG_ERR_TIMEOUT -2
#define TIKU_TRNG_ERR_NOT_READY -3
/** @brief Boot-only release of ROM ADC ownership, before any driver starts. */
void tiku_trng_arch_init(void);
/** @brief Fill 1..256 bytes with internal SAR entropy; zero output on failure.
 * @note Foreground only; refuses an enabled ADC or concurrent analog command.
 * Interrupts stay masked during collection; each word waits at least 3 ms.
 * This entropy source does not route or sample an external board pin.
 */
int tiku_trng_arch_read_bytes(uint8_t *out, size_t length);
/** @brief Read one word, with the same ownership and failure rules. */
int tiku_trng_arch_read_u32(uint32_t *out);
#endif
