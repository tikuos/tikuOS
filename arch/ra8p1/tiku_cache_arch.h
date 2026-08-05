/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cache_arch.h - Cortex-M85 cache control.
 *
 * Enabling the caches is only meaningful once the MPU has programmed region
 * attributes, since MAIR is what makes SRAM cacheable at all.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_CACHE_ARCH_H_
#define TIKU_RA8P1_CACHE_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/** @brief Invalidate and enable both caches; idempotent. */
void tiku_ra8p1_cache_enable(void);

/** @brief Clean the data cache out to memory, then disable both caches. */
void tiku_ra8p1_cache_disable(void);

/**
 * @brief Report cache state.
 *
 * @return Bit 0 set when the I-cache is on, bit 1 when the D-cache is
 */
uint32_t tiku_ra8p1_cache_state(void);

/**
 * @brief Push a range out of the data cache into memory.
 *
 * @param addr  Range start; rounded down to a line
 * @param len   Range length in bytes
 */
void tiku_ra8p1_dcache_clean(const void *addr, size_t len);

/**
 * @brief Drop a range from the data cache so the next read refetches.
 *
 * @param addr  Range start; rounded down to a line
 * @param len   Range length in bytes
 */
void tiku_ra8p1_dcache_invalidate(const void *addr, size_t len);

/**
 * @brief Write back then drop the lines covering @p addr.
 *
 * The safe choice after a DMA write into a buffer that is not cache-line
 * aligned: a plain invalidate would DISCARD dirty data belonging to whatever
 * shares the first and last lines.
 */
void tiku_ra8p1_dcache_clean_invalidate(const void *addr, size_t len);

/**
 * @brief Invalidate the whole instruction cache.
 *
 * For code that changed under the cache -- a written MRAM page, a loaded
 * module.  No-op when the I-cache is off.
 */
void tiku_ra8p1_icache_invalidate(void);

#endif /* TIKU_RA8P1_CACHE_ARCH_H_ */
