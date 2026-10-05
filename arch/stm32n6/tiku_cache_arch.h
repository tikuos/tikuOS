/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cache_arch.h - Cortex-M55 cache control for the STM32N6.
 *
 * The boot ROM hands over with both caches off on the serial-boot path; these
 * calls enable them and keep DMA buffers and the XSPI window coherent.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_CACHE_ARCH_H_
#define TIKU_STM32N6_CACHE_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Enable each cache that is off, invalidating it first.
 *
 * A cache that is already on is left untouched.
 */
void tiku_stm32n6_cache_enable(void);

/** @brief Disable both caches; the data cache is cleaned and invalidated
 *         after its enable bit clears, so no line is dirtied in between. */
void tiku_stm32n6_cache_disable(void);

/**
 * @brief Report cache state.
 *
 * @return Bit 0 set when the I-cache is on, bit 1 when the D-cache is
 */
uint32_t tiku_stm32n6_cache_state(void);

/**
 * @brief Write a range's dirty data-cache lines back to memory.
 *
 * No-op while the D-cache is off.
 *
 * @param addr  Range start; rounded down to a line
 * @param len   Range length in bytes
 */
void tiku_stm32n6_dcache_clean(const void *addr, size_t len);

/**
 * @brief Drop a range from the data cache so the next read refetches.
 *
 * No-op while the D-cache is off.
 *
 * @param addr  Range start; rounded down to a line
 * @param len   Range length in bytes
 * @note Whole 32-byte lines are dropped without write-back: dirty data that
 *       shares the first or last line with the range is lost.
 */
void tiku_stm32n6_dcache_invalidate(const void *addr, size_t len);

/**
 * @brief Invalidate the whole instruction cache.
 *
 * For code that changed under the cache, such as a loaded module.  No-op
 * while the I-cache is off.
 */
void tiku_stm32n6_icache_invalidate(void);

#endif /* TIKU_STM32N6_CACHE_ARCH_H_ */
