/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_cache.c - freshness cache (read coalescing) implementation.
 *
 * Holds up to TIKU_VFS_CACHE_MAX renderings in SRAM, each served until its
 * node's freshness window ends or tiku_vfs_notify() drops it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Three properties the cache holds:
 *
 *  1. Wrap-safe freshness.  Timestamps pair the 32-bit seconds count with
 *     the tick, which is 16 bits on MSP430 and wraps every 512 s at 128 Hz.
 *     The coarse guard CACHE_MAX_AGE_S (30 s) declares anything older stale,
 *     well inside the wrap, so the tick subtraction below is exact.
 *     Freshness windows must stay under CACHE_MAX_AGE_S.
 *
 *  2. Race against notify.  A miss runs the handler outside the atomic
 *     section, because a conversion is slow.  A global notify sequence is
 *     captured before the call and re-checked under the mask before the
 *     store: a sample that a notify overlapped is returned but not stored.
 *
 *  3. Concurrency.  invalidate() may run from ISR context while
 *     get()/sample() store from process context, so every table mutation is
 *     bracketed by tiku_atomic_enter()/exit(), and an ISR sees no torn slot.
 */


#include "tiku_vfs_cache.h"

#if TIKU_VFS_CACHE_ENABLE

#include <kernel/timers/tiku_clock.h>
#include <hal/tiku_cpu.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/** Coarse upper bound (seconds) on a fresh entry's age.  Bounds the tick
 *  diff to a non-wrapping interval; any window must be smaller than this. */
#define CACHE_MAX_AGE_S  30u

/** @brief One cached rendering. */
typedef struct {
    const tiku_vfs_node_t *node;     /**< NULL = free slot              */
    uint32_t               stamp_s;  /**< tiku_clock_seconds() at sample */
    tiku_clock_time_t      stamp_t;  /**< tiku_clock_time() at sample    */
    uint8_t                len;      /**< cached rendering length        */
    char                   text[TIKU_VFS_CACHE_TEXTLEN]; /**< no terminator */
} cache_slot_t;

static cache_slot_t      cache[TIKU_VFS_CACHE_MAX];
static volatile uint16_t notify_seq;   /* bumped by invalidate and flush */
static uint32_t          stat_hits;
static uint32_t          stat_misses;

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Is slot @p s still inside its node's freshness window? */
static int slot_fresh(const cache_slot_t *s)
{
    uint32_t now_s = (uint32_t)tiku_clock_seconds();
    tiku_clock_time_t now_t;

    if ((uint32_t)(now_s - s->stamp_s) > CACHE_MAX_AGE_S) {
        return 0;                      /* too old (or tick would alias) */
    }
    now_t = tiku_clock_time();          /* exact: < CACHE_MAX_AGE_S of ticks */
    return (tiku_clock_time_t)(now_t - s->stamp_t) < s->node->desc->fresh_ticks;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

int tiku_vfs_cache_get(const tiku_vfs_node_t *node, char *buf, size_t max)
{
    int ret = -1;
    int i;

    tiku_atomic_enter();
    for (i = 0; i < TIKU_VFS_CACHE_MAX; i++) {
        if (cache[i].node == node) {
            if (slot_fresh(&cache[i])) {
                size_t len = cache[i].len;
                if (max > 0) {
                    if (len >= max) len = max - 1;
                    memcpy(buf, cache[i].text, len);
                    buf[len] = '\0';
                }
                ret = (int)cache[i].len;   /* snprintf-style length */
            } else {
                cache[i].node = NULL;      /* drop the expired entry */
            }
            break;
        }
    }
    tiku_atomic_exit();

    if (ret >= 0) {
        stat_hits++;
    }
    return ret;
}

int tiku_vfs_cache_sample(const tiku_vfs_node_t *node, char *buf, size_t max)
{
    uint16_t seq0 = notify_seq;          /* captured before sampling */
    int n = node->read(buf, max);        /* slow handler, unmasked  */

    stat_misses++;

    /* Store only complete renderings: n < max means the snprintf-style
     * handler did not truncate (buf holds all n bytes), and n < TEXTLEN
     * means it fits a slot.  A truncated read is returned but not cached. */
    if (n > 0 && (size_t)n < max && (size_t)n < TIKU_VFS_CACHE_TEXTLEN) {
        tiku_atomic_enter();
        if (notify_seq == seq0) {        /* no notify raced the sample */
            int i, slot = 0;
            int found = -1, freei = -1;
            uint32_t oldest = 0xFFFFFFFFu;

            for (i = 0; i < TIKU_VFS_CACHE_MAX; i++) {
                if (cache[i].node == node) {
                    found = i;
                    break;
                }
                if (cache[i].node == NULL && freei < 0) {
                    freei = i;
                }
                if (cache[i].stamp_s < oldest) {
                    oldest = cache[i].stamp_s;
                    slot = i;             /* oldest sample: the victim */
                }
            }
            if (found >= 0) {
                slot = found;
            } else if (freei >= 0) {
                slot = freei;
            }
            cache[slot].node = node;
            cache[slot].stamp_s = (uint32_t)tiku_clock_seconds();
            cache[slot].stamp_t = tiku_clock_time();
            /* The guard above keeps n below TIKU_VFS_CACHE_TEXTLEN, so n
             * fits uint8_t while TEXTLEN is at most 256; a larger TEXTLEN
             * needs a wider `len`. */
            cache[slot].len = (uint8_t)n;
            memcpy(cache[slot].text, buf, (size_t)n);
        }
        tiku_atomic_exit();
    }
    return n;
}

void tiku_vfs_cache_invalidate(const tiku_vfs_node_t *node)
{
    int i;

    if (node == NULL) {
        return;
    }
    tiku_atomic_enter();
    notify_seq++;                        /* bar any in-flight sample store */
    for (i = 0; i < TIKU_VFS_CACHE_MAX; i++) {
        if (cache[i].node == node) {
            cache[i].node = NULL;
            break;
        }
    }
    tiku_atomic_exit();
}

void tiku_vfs_cache_flush(void)
{
    int i;

    tiku_atomic_enter();
    notify_seq++;
    for (i = 0; i < TIKU_VFS_CACHE_MAX; i++) {
        cache[i].node = NULL;
    }
    tiku_atomic_exit();
}

void tiku_vfs_cache_stats(uint32_t *hits, uint32_t *misses, uint8_t *used)
{
    if (hits != NULL) {
        *hits = stat_hits;
    }
    if (misses != NULL) {
        *misses = stat_misses;
    }
    if (used != NULL) {
        uint8_t u = 0;
        int i;
        for (i = 0; i < TIKU_VFS_CACHE_MAX; i++) {
            if (cache[i].node != NULL) {
                u++;
            }
        }
        *used = u;
    }
}

#else /* !TIKU_VFS_CACHE_ENABLE: no-op stubs, every read calls its handler */

int tiku_vfs_cache_get(const tiku_vfs_node_t *node, char *buf, size_t max)
{
    (void)node; (void)buf; (void)max;
    return -1;
}

int tiku_vfs_cache_sample(const tiku_vfs_node_t *node, char *buf, size_t max)
{
    return node->read(buf, max);
}

void tiku_vfs_cache_invalidate(const tiku_vfs_node_t *node) { (void)node; }
void tiku_vfs_cache_flush(void) { }

void tiku_vfs_cache_stats(uint32_t *hits, uint32_t *misses, uint8_t *used)
{
    if (hits != NULL) { *hits = 0; }
    if (misses != NULL) { *misses = 0; }
    if (used != NULL) { *used = 0; }
}

#endif /* TIKU_VFS_CACHE_ENABLE */
