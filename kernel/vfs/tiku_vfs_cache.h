/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_cache.h - Freshness cache (read coalescing) for the VFS
 *
 * A small SRAM table that remembers the most recent rendered value of
 * nodes whose descriptor declares a freshness window (desc.fresh_ticks
 * > 0).  A read that lands inside the window is served from SRAM
 * instead of re-invoking the handler -- which, for a LIVE/PERIPH node
 * such as /dev/adc/temp, avoids waking the ADC.  This is "read
 * coalescing": several reads of the same sensor within one window cost
 * a single conversion.
 *
 * NOT to be confused with kernel/memory/tiku_cache.c, the FRAM
 * write-back cache -- an entirely different mechanism.
 *
 * The cache caches the rendered TEXT at the handler boundary (inside
 * tiku_vfs_read_node), so every text consumer -- the shell, the rules
 * tick, `watch`, BASIC -- benefits; the typed read path
 * (tiku_vfs_read_val) decodes the cached text and benefits too.
 *
 * Coherence rides the event bus: tiku_vfs_notify() invalidates a
 * node's entry, so a successful write or a driver edge never leaves a
 * stale value behind.  The cache is .bss -- a power loss / LPMx.5 wake
 * destroys it, which for a cache is exactly correct.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_CACHE_H_
#define TIKU_VFS_CACHE_H_

#include "tiku_vfs.h"
#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Master enable.  When 0 the cache compiles to no-ops and every
 *         read samples its handler (the pre-cache behaviour). */
#ifndef TIKU_VFS_CACHE_ENABLE
#define TIKU_VFS_CACHE_ENABLE  1
#endif

/** @brief Number of cache slots (SRAM).  Each slot is ~24 bytes. */
#ifndef TIKU_VFS_CACHE_MAX
#define TIKU_VFS_CACHE_MAX  4
#endif

/** @brief Longest rendering the cache will hold.  Renderings at or above
 *         this length are simply not cached (the read still succeeds). */
#ifndef TIKU_VFS_CACHE_TEXTLEN
#define TIKU_VFS_CACHE_TEXTLEN  16
#endif

/*---------------------------------------------------------------------------*/
/* API                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Serve @p node from cache if a fresh entry exists.
 *
 * Only meaningful for cacheable nodes (desc != NULL && fresh_ticks > 0);
 * tiku_vfs_read_node() gates on that before calling.  An expired entry
 * is dropped in passing (self-cleaning).
 *
 * @param node  The node being read
 * @param buf   Output buffer (up to @p max bytes written on a hit)
 * @param max   Buffer capacity
 * @return Bytes (snprintf-style length) on a hit, or -1 on miss/expired.
 */
int tiku_vfs_cache_get(const tiku_vfs_node_t *node, char *buf, size_t max);

/**
 * @brief Miss path: invoke the handler, then cache the rendering.
 *
 * Calls @c node->read(buf,max), and if the rendering fits and no
 * concurrent tiku_vfs_notify() raced the sample, stores it.  Returns
 * the handler's result unchanged.
 */
int tiku_vfs_cache_sample(const tiku_vfs_node_t *node, char *buf, size_t max);

/**
 * @brief Drop @p node's cached entry and bar any in-flight sample for it
 *        from being stored stale.  ISR-safe.  Called by tiku_vfs_notify().
 */
void tiku_vfs_cache_invalidate(const tiku_vfs_node_t *node);

/** @brief Drop every cached entry (e.g. before a deep-sleep transition). */
void tiku_vfs_cache_flush(void);

/**
 * @brief Observability counters.  Any pointer may be NULL.
 * @param hits    Out: cumulative cache hits
 * @param misses  Out: cumulative cache misses (cacheable reads that sampled)
 * @param used    Out: occupied slots [0, TIKU_VFS_CACHE_MAX]
 */
void tiku_vfs_cache_stats(uint32_t *hits, uint32_t *misses, uint8_t *used);

#endif /* TIKU_VFS_CACHE_H_ */
