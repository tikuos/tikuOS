/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - S31 health-checked LP random-source access.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_TRNG_ARCH_H_
#define TIKU_S31_TRNG_ARCH_H_
#include <stddef.h>
#include <stdint.h>
/** @brief Configure the LP noise source with health tests; kernel foreground only.
 * Returns zero after startup checks, or -1 with the source disabled. This
 * port owns the RNG until reset; repeat initialization retains a latched fault.
 */
int tiku_s31_trng_init(void);
/** @brief Fill at most 4096 bytes; return -1 on invalid context or a health fault.
 * IRQ calls are refused. On a source fault the output is zeroed and further
 * requests fail until reset. Startup and reads do not establish entropy certification.
 */
int tiku_trng_arch_read_bytes(uint8_t *out, size_t length);
/** @brief Read one word through the same health checks; return zero or -1. */
int tiku_trng_arch_read_u32(uint32_t *out);
#endif
