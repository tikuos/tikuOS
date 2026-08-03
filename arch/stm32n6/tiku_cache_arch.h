/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cache_arch.h - Cortex-M55 cache control for the STM32N6.
 *
 * The boot ROM hands over with the caches off on the serial-boot path, so
 * enabling them is the port's job, as is coherency with DMA and the XSPI map.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_CACHE_ARCH_H_
#define TIKU_STM32N6_CACHE_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/** @brief Invalidate and enable both caches; idempotent. */
void tiku_stm32n6_cache_enable(void);

/** @brief Clean the data cache out to memory, then disable both caches. */
void tiku_stm32n6_cache_disable(void);

/**
 * @brief Report cache state.
 *
 * @return Bit 0 set when the I-cache is on, bit 1 when the D-cache is
 */
uint32_t tiku_stm32n6_cache_state(void);

/**
 * @brief Push a range out of the data cache into memory.
 *
 * @param addr  Range start; rounded down to a line
 * @param len   Range length in bytes
 */
void tiku_stm32n6_dcache_clean(const void *addr, size_t len);

/**
 * @brief Drop a range from the data cache so the next read refetches.
 *
 * @param addr  Range start; rounded down to a line
 * @param len   Range length in bytes
 */
void tiku_stm32n6_dcache_invalidate(const void *addr, size_t len);

#endif /* TIKU_STM32N6_CACHE_ARCH_H_ */
