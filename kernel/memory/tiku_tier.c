/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tier.c - tier-aware memory allocator.
 *
 * Reserves an interval of a tier's backing span (SRAM, NVM, HIFRAM or PSRAM)
 * and builds an arena or pool over it.  AUTO picks SRAM or HIFRAM, and PSRAM
 * only when the caller allows external memory; NVM needs an explicit request.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_mem.h"
#define TIKU_MEM_RESERVATION_IMPL
#include "tiku_mem_internal.h"
#include "tiku_reclaim_internal.h"
#if TIKU_MEM_RECLAIM_ENABLE
#include "hal/tiku_cpu.h"
#endif
#include "tiku_nvm_region.h"
#include "tiku_layout.h"
#include <stddef.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* BACKING POOLS                                                             */
/*---------------------------------------------------------------------------*/

/*
 * Each tier's backing span.  SRAM is a linker carve or a static array (below);
 * NVM is a durable array on MSP430 and the front of the carved NVM region
 * elsewhere (see tier_wire_all()); HIFRAM is an array in MSP430's upper FRAM
 * bank; PSRAM is attached at run time.  The arrays are aligned to
 * TIKU_MEM_ARCH_ALIGNMENT.
 */

#if defined(TIKU_TIER_SRAM_DERIVED)
/* Every port but MSP430: the linker carves the span from what the tier's bank
 * has left after the statics (arch/common/tiku_sram_layout.ld).  Ambiq and the
 * nRF54LM20 add a second span of spare RAM (TIKU_TIER_SRAM_EXTRA, below).
 * There is no array, so nothing for the crt to zero; the allocator does not
 * promise zeroed memory. */
extern uint8_t __tier_sram_start;
extern uint8_t __tier_sram_end;
#define TIER_SRAM_BUF  (&__tier_sram_start)
#define TIER_SRAM_CAP  ((tiku_mem_arch_size_t)(&__tier_sram_end - \
                                               &__tier_sram_start))
#else
/**
 * @brief SRAM tier backing where the linker does not carve one (MSP430, host
 *        builds): TIKU_TIER_SRAM_SIZE bytes of .bss.
 */
static uint8_t __attribute__((aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_sram_buf[TIKU_TIER_SRAM_SIZE];
#endif

#ifndef TIER_SRAM_BUF
#define TIER_SRAM_BUF  (tier_sram_buf)
#define TIER_SRAM_CAP  ((tiku_mem_arch_size_t)TIKU_TIER_SRAM_SIZE)
#endif

#if defined(TIKU_TIER_SRAM_EXTRA)
#ifndef TIER_SRAM_EXTRA_BUF
extern uint8_t __tier_sram_extra_start;
extern uint8_t __tier_sram_extra_end;
#define TIER_SRAM_EXTRA_BUF (&__tier_sram_extra_start)
#define TIER_SRAM_EXTRA_CAP ((tiku_mem_arch_size_t)(\
    (uintptr_t)&__tier_sram_extra_end - (uintptr_t)&__tier_sram_extra_start))
#endif
#endif

/**
 * @brief NVM tier backing on MSP430: a durable array, separate from the
 *        pinned region backend.  Other boards use the carved region.
 */
/* There is no RAM fallback: a board with neither MSP430's FRAM nor a carved
 * region has no NVM tier, and a request for one fails at the call site. */
#ifdef PLATFORM_MSP430
static TIKU_DURABLE uint8_t __attribute__((aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_nvm_buf[TIKU_TIER_NVM_SIZE] = {0};
#endif

/**
 * @brief Backing store for the HIFRAM (upper FRAM bank) tier.
 *
 * Declared only when the device has an upper bank and the build is large-model,
 * because the section attribute targets an output section that only exists
 * then.  Elsewhere the array is absent and creating on HIFRAM returns NOMEM.
 */
#if defined(TIKU_DEVICE_HAS_HIFRAM) && TIKU_DEVICE_HAS_HIFRAM && \
    defined(TIKU_MEMORY_MODEL_LARGE) && TIKU_MEMORY_MODEL_LARGE
TIKU_HIFRAM_BSS
static uint8_t __attribute__((aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_hifram_buf[TIKU_TIER_HIFRAM_SIZE];
/**
 * @brief Compile-time flag: 1 when the HIFRAM tier pool exists.
 *
 * Guards every site naming the backing array or initialising its tier slot, so
 * a build without HIFRAM never references the absent array.
 */
#define TIKU_TIER_HIFRAM_AVAILABLE 1
#else
#define TIKU_TIER_HIFRAM_AVAILABLE 0
#endif

/*---------------------------------------------------------------------------*/
/* INTERNAL STATE                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Per-span capacity and reservation counters
 *
 * The separate reservation table describes occupied intervals. Free gaps are
 * computed from those intervals; there is no bump cursor to rewind on release.
 */
typedef struct {
    uint8_t              *buf;         /**< Backing pool start */
    tiku_mem_arch_size_t  capacity;    /**< Total pool size in bytes */
    tiku_mem_arch_size_t  used;        /**< Bytes in live reservations/holds */
    tiku_mem_arch_size_t  peak;        /**< Lifetime high-water mark */
    tiku_mem_arch_size_t  alloc_count; /**< Number of sub-allocations */
    tiku_mem_arch_size_t  fail_count;  /**< Carves refused for lack of room */
    uint8_t               initialized; /**< Non-zero while it has backing */
#if TIKU_MEM_RECLAIM_ENABLE
    uint8_t fenced;                    /**< Coordinator metadata fence */
#endif
} tier_pool_state_t;

/**
 * @brief Primary span state, indexed by tiku_mem_tier_t.
 *
 * The AUTO slot is never indexed at runtime -- AUTO resolves to a concrete tier
 * first -- but it stays in the array to keep the enum-to-index mapping direct.
 * In .bss, so every field starts at zero before init runs.
 */
static tier_pool_state_t tier_state[TIKU_MEM_TIER_COUNT];

#if defined(TIKU_TIER_SRAM_EXTRA)
/* One additional, disjoint SRAM span. No overhead on single-span targets. */
static tier_pool_state_t sram_extra;
static tiku_mem_arch_size_t sram_peak;
#endif

/** @brief Resolve a concrete tier's backing span, without merging addresses. */
static tier_pool_state_t *tier_span(tiku_mem_tier_t tier, uint8_t index)
{
    if (tier == TIKU_MEM_AUTO || (unsigned)tier >= TIKU_MEM_TIER_COUNT) {
        return NULL;
    }
    if (index == 0u) {
        return &tier_state[tier];
    }
#if defined(TIKU_TIER_SRAM_EXTRA)
    if (tier == TIKU_MEM_SRAM && index == 1u) {
        return &sram_extra;
    }
#endif
    return NULL;
}


/**
 * @brief Raise the span's peak, and the combined SRAM peak, to the bytes now
 *        reserved; free alignment gaps do not count.
 */
static void tier_note_peak(tier_pool_state_t *ts)
{
    if (ts->used > ts->peak) ts->peak = ts->used;
#if defined(TIKU_TIER_SRAM_EXTRA)
    if (ts == &tier_state[TIKU_MEM_SRAM] || ts == &sram_extra) {
        tiku_mem_arch_size_t used = tier_state[TIKU_MEM_SRAM].used + sram_extra.used;
        if (used > sram_peak) sram_peak = used;
    }
#endif
}

#ifndef TIKU_MEM_GENERATION_MAX
#define TIKU_MEM_GENERATION_MAX UINT32_MAX
#endif
#if TIKU_MEM_MAX_RESERVATIONS < 1 || TIKU_MEM_MAX_RESERVATIONS > 65535
#error Invalid reservation table size
#endif

/**
 * @brief One reservation: an interval of a span and the descriptor it backs,
 *        kept in kernel RAM, never in the backing it describes.
 *
 * state is 0 free, 1 live, 2 held (no descriptor), 3 pool being built, or
 * 4 credit (metadata only, no span).  generation grows with each use of the
 * slot, and a slot at TIKU_MEM_GENERATION_MAX is never reused.
 */
typedef struct {
    const void *descriptor;
    tier_pool_state_t *span;
    tiku_mem_arch_size_t offset, length, alignment;
    tiku_mem_arch_size_t block_size, block_count;
    uint32_t generation;
    tiku_mem_owner_t owner;
    uint16_t owner_slot;
    uint8_t kind, state, allocation_class, tier;
} backing_record_t;
static backing_record_t reservations[TIKU_MEM_MAX_RESERVATIONS];

/**
 * @brief Free every record on @p span, or on every span when NULL.
 *
 * Generations are kept, so a handle to a freed record stays stale.
 */
static void records_invalidate(tier_pool_state_t *span)
{
    unsigned i;
    for (i = 0; i < TIKU_MEM_MAX_RESERVATIONS; i++) {
        if (span == NULL || reservations[i].span == span) {
            /* Do not dereference a descriptor: its caller may already be gone.
             * Generations, and with them retired slots, survive the reset. */
            reservations[i].state = 0;
        }
    }
}

int tiku_backing_busy(const void *descriptor)
{
    unsigned i;
    for (i = 0; i < TIKU_MEM_MAX_RESERVATIONS; i++) {
        if (reservations[i].state && reservations[i].descriptor == descriptor)
            return 1;
    }
    return 0;
}

int tiku_backing_mutable(tiku_mem_backing_t h)
{
#if TIKU_MEM_RECLAIM_ENABLE
    const backing_record_t *r;
    if (!h.slot_plus_one || h.slot_plus_one > TIKU_MEM_MAX_RESERVATIONS) return 0;
    r = &reservations[h.slot_plus_one - 1u];
    return r->state == 1 && r->generation == h.generation &&
           tiku_reclaim_owner_access(r->owner);
#else
    (void)h;
    return 1;
#endif
}

/**
 * @brief Live record that handle @p h gives @p descriptor, or NULL when the
 *        handle is stale, names another object or kind, or the span is gone.
 */
static backing_record_t *record_get(const void *descriptor,
        tiku_mem_backing_t h, uint8_t kind)
{
    backing_record_t *r;
    if (h.slot_plus_one == 0u || h.slot_plus_one > TIKU_MEM_MAX_RESERVATIONS ||
        h.generation == 0u) return NULL;
    r = &reservations[h.slot_plus_one - 1u];
    if (r->state != 1u || r->generation != h.generation ||
        r->descriptor != descriptor || r->kind != kind ||
        !r->span->initialized) return NULL;
    return r;
}

int tiku_backing_valid(const void *descriptor, tiku_mem_backing_t h, uint8_t kind)
{
    const backing_record_t *r = record_get(descriptor, h, kind);
    if (r == NULL) return 0;
    if (kind == TIKU_BACKING_ARENA) {
        const tiku_arena_t *a = descriptor;
        return a->buf == r->span->buf + r->offset && a->capacity == r->length &&
               a->tier == r->tier && a->claim_base == NULL;
    }
    if (kind == TIKU_BACKING_POOL) {
        const tiku_pool_t *p = descriptor;
        return p->buf == r->span->buf + r->offset &&
               p->block_size == r->block_size && p->block_count == r->block_count &&
               p->tier == r->tier && p->nvm == (r->tier == TIKU_MEM_NVM);
    }
    return 0;
}

tiku_mem_err_t tiku_backing_release(const void *descriptor,
        tiku_mem_backing_t h, uint8_t kind)
{
    backing_record_t *r = record_get(descriptor, h, kind);
    unsigned i;
    if (r == NULL) return TIKU_MEM_ERR_INVALID;
#if TIKU_MEM_RECLAIM_ENABLE
    if (r->span->fenced) return TIKU_MEM_ERR_BUSY;
#endif
    if (r->allocation_class == TIKU_MEM_FIXED) {
        for (i = 0; i < TIKU_MEM_MAX_RESERVATIONS; i++) {
            const backing_record_t *other = &reservations[i];
            if (other->state && other->span == r->span &&
                other->allocation_class == TIKU_MEM_FIXED &&
                other->offset > r->offset) return TIKU_MEM_ERR_BUSY;
        }
    }
    r->span->used -= r->length;
    r->state = 0;
    return TIKU_MEM_OK;
}

/** @brief Index of a free slot not yet retired, or -1 when none is left. */
static int record_available(void)
{
    unsigned i;
    for (i = 0; i < TIKU_MEM_MAX_RESERVATIONS; i++) {
        if (!reservations[i].state &&
            reservations[i].generation < TIKU_MEM_GENERATION_MAX) return (int)i;
    }
    return -1;
}

tiku_mem_err_t tiku_mem_reservation_next(uint16_t *cursor,
                                         tiku_mem_reservation_info_t *info)
{
    unsigned i;
    if (cursor == NULL || info == NULL) return TIKU_MEM_ERR_INVALID;
    for (i = *cursor; i < TIKU_MEM_MAX_RESERVATIONS; i++) {
        const backing_record_t *r = &reservations[i];
        uint8_t span_index = 0;
        if (!r->state || r->state == 4u) continue;
        while (tier_span((tiku_mem_tier_t)r->tier, span_index) != r->span) span_index++;
        info->handle = (tiku_mem_backing_t){r->generation, (uint16_t)(i + 1)};
        info->owner = r->owner;
        info->owner_slot = r->owner_slot;
        info->state = r->state;
        info->tier = (tiku_mem_tier_t)r->tier;
        info->span_index = span_index;
        info->kind = r->kind;
        info->allocation_class = r->allocation_class;
        info->offset = r->offset;
        info->length = r->length;
        info->alignment = r->alignment;
        *cursor = (uint16_t)(i + 1);
        return TIKU_MEM_OK;
    }
    return TIKU_MEM_ERR_NOT_FOUND;
}

/**
 * @brief Lowest-offset record on @p span at or above @p at, or NULL.
 *
 * Address order comes from a bounded scan of the record table; the backing
 * holds no list and there is no address-sized bitmap.
 */
static const backing_record_t *record_next(const tier_pool_state_t *span,
                                           tiku_mem_arch_size_t at)
{
    const backing_record_t *best = NULL;
    unsigned i;
    for (i = 0; i < TIKU_MEM_MAX_RESERVATIONS; i++) {
        const backing_record_t *r = &reservations[i];
        if (r->state && r->span == span && r->offset >= at &&
            (best == NULL || r->offset < best->offset)) best = r;
    }
    return best;
}

tiku_mem_err_t tiku_tier_span_space(tiku_mem_tier_t tier, uint8_t index,
                                    tiku_mem_space_t *out)
{
    const tier_pool_state_t *span = tier_span(tier, index);
    const backing_record_t *r;
    tiku_mem_space_t s = {0};
    tiku_mem_arch_size_t at = 0, gap;
    if (out == NULL) return TIKU_MEM_ERR_INVALID;
    if (span == NULL || !span->initialized) return TIKU_MEM_ERR_NOT_FOUND;
    while ((r = record_next(span, at)) != NULL) {
        gap = r->offset - at;
        s.free_bytes += gap;
        if (gap > s.largest_gap) s.largest_gap = gap;
        if (r->state == 1) { s.live_bytes += r->length; s.live_records++; }
        else s.held_bytes += r->length;
        at = r->offset + r->length;
    }
    gap = span->capacity - at;
    s.free_bytes += gap;
    if (gap > s.largest_gap) s.largest_gap = gap;
    s.split_free = s.free_bytes - s.largest_gap;
    *out = s;
    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* PSRAM TIER (ATTACHED AT RUN TIME)                                         */
/*---------------------------------------------------------------------------*/

tiku_mem_err_t tiku_tier_attach_psram(void *base, tiku_mem_arch_size_t size)
{
    unsigned tier;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
#if TIKU_MEM_RECLAIM_ENABLE
    if (tier_state[TIKU_MEM_PSRAM].fenced) return TIKU_MEM_ERR_BUSY;
#endif
    if (base == NULL || size == 0u || size > UINTPTR_MAX - (uintptr_t)base) {
        return TIKU_MEM_ERR_INVALID;
    }
    if (tier_state[TIKU_MEM_PSRAM].initialized) {
        return TIKU_MEM_ERR_INVALID;    /* already attached */
    }
    for (tier = 0; tier < TIKU_MEM_TIER_COUNT; tier++) {
        tier_pool_state_t *span;
        uint8_t si;
        if (tier == TIKU_MEM_AUTO || tier == TIKU_MEM_PSRAM) continue;
        for (si = 0; (span = tier_span((tiku_mem_tier_t)tier, si)) != NULL; si++) {
            uintptr_t a = (uintptr_t)base, b = (uintptr_t)span->buf;
            if (span->initialized && span->capacity &&
                ((a >= b && a - b < span->capacity) ||
                 (a < b && b - a < size))) return TIKU_MEM_ERR_INVALID;
        }
    }
    tier_state[TIKU_MEM_PSRAM].buf         = (uint8_t *)base;
    tier_state[TIKU_MEM_PSRAM].capacity    = size;
    tier_state[TIKU_MEM_PSRAM].used      = 0;
    tier_state[TIKU_MEM_PSRAM].peak        = 0;
    tier_state[TIKU_MEM_PSRAM].alloc_count = 0;
    tier_state[TIKU_MEM_PSRAM].fail_count  = 0;
    tier_state[TIKU_MEM_PSRAM].initialized = 1;
    return TIKU_MEM_OK;
}

tiku_mem_err_t tiku_tier_detach_psram(int force)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
#if TIKU_MEM_RECLAIM_ENABLE
    if (tier_state[TIKU_MEM_PSRAM].fenced && !force) return TIKU_MEM_ERR_BUSY;
    if (force) tiku_reclaim_detached(TIKU_MEM_PSRAM, 0);
#endif
    if (!tier_state[TIKU_MEM_PSRAM].initialized) {
        return TIKU_MEM_OK;            /* already gone: idempotent */
    }
    if ((tier_state[TIKU_MEM_PSRAM].used != 0u) && !force) {
        return TIKU_MEM_ERR_BUSY;      /* live reservations would be stranded */
    }
    records_invalidate(&tier_state[TIKU_MEM_PSRAM]);
    tier_state[TIKU_MEM_PSRAM].initialized = 0;
    tier_state[TIKU_MEM_PSRAM].buf         = NULL;
    tier_state[TIKU_MEM_PSRAM].capacity    = 0;
    tier_state[TIKU_MEM_PSRAM].used      = 0;
    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* TIER INIT                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Wire every tier to its backing span and clear its records and
 *        counters.
 *
 * Destructive: anything handed out by the arena and pool creators is
 * orphaned.  The NVM backing is not zeroed, so durable contents survive.
 * tiku_tier_init() runs it once; tiku_tier_reset() on demand.
 */
static void tier_wire_all(void)
{
    records_invalidate(NULL);
    /* PSRAM is not wired here: tiku_tier_attach_psram() attaches it later,
     * and a reset drops any attachment. */
    tier_state[TIKU_MEM_PSRAM].initialized = 0;
    tier_state[TIKU_MEM_PSRAM].buf         = NULL;
    tier_state[TIKU_MEM_PSRAM].capacity    = 0;
    tier_state[TIKU_MEM_PSRAM].used      = 0;

    tier_state[TIKU_MEM_SRAM].buf         = TIER_SRAM_BUF;
    tier_state[TIKU_MEM_SRAM].capacity    = TIER_SRAM_CAP;
    tier_state[TIKU_MEM_SRAM].used      = 0;
    tier_state[TIKU_MEM_SRAM].peak        = 0;
    tier_state[TIKU_MEM_SRAM].alloc_count = 0;
    tier_state[TIKU_MEM_SRAM].fail_count  = 0;
    tier_state[TIKU_MEM_SRAM].initialized = 1;
#if defined(TIKU_TIER_SRAM_EXTRA)
    memset(&sram_extra, 0, sizeof sram_extra);
    sram_extra.buf = TIER_SRAM_EXTRA_BUF;
    sram_extra.capacity = TIER_SRAM_EXTRA_CAP;
    sram_extra.initialized = 1;
    sram_peak = 0;
#endif
#if defined(TIKU_TIER_POISON)
    /*
     * On the parts whose tier the linker carves, the span sits outside the
     * crt's zero loop, so its contents are whatever the last boot left.  The
     * allocator does not promise zeroed memory: tiku_arena_alloc() has no
     * memset and the NVM tier backing is not zeroed.  Filling with 0xA5 makes
     * a caller that reads before it writes fail in a poisoned build.
     */
    {
        size_t pi;
        uint8_t si;
        tier_pool_state_t *span;
        for (si = 0; (span = tier_span(TIKU_MEM_SRAM, si)) != NULL; si++) {
            for (pi = 0; pi < (size_t)span->capacity; pi++) {
                span->buf[pi] = 0xA5u;
            }
        }
    }
#endif

#ifdef PLATFORM_MSP430
    /* MSP430: the NVM tier is the durable FRAM array above. */
    tier_state[TIKU_MEM_NVM].buf         = tier_nvm_buf;
    tier_state[TIKU_MEM_NVM].capacity    = TIKU_TIER_NVM_SIZE;
    tier_state[TIKU_MEM_NVM].initialized = 1;
#else
    {
        /* tiku_nvm_backend_get() says whether this board carved a region (its
         * weak default returns NULL), so a port has an NVM tier once it
         * supplies a backend and none before.
         *
         * The tier owns the front of the region up to the store's base, as
         * the layout service decided at boot.  A held store publishes no
         * tier: bytes whose owner is unknown are not handed out. */
        const tiku_nvm_backend_t *rgn = tiku_nvm_backend_get();
        const tiku_layout_state_t *ls = tiku_layout_state();

        if (rgn != NULL && rgn->base != NULL && ls->tier != 0u &&
            rgn->size > (size_t)ls->tier &&
            ls->store == TIKU_LAYOUT_STORE_READY) {
            tier_state[TIKU_MEM_NVM].buf      = rgn->base;
            tier_state[TIKU_MEM_NVM].capacity =
                (tiku_mem_arch_size_t)ls->tier;
            tier_state[TIKU_MEM_NVM].initialized = 1;
        } else {
            /* No region, no tier share of it, or a held store: the tier stays
             * uninitialised, so a create on it fails and tiku_tier_stats()
             * reports it absent. */
            tier_state[TIKU_MEM_NVM].buf         = NULL;
            tier_state[TIKU_MEM_NVM].capacity    = 0u;
            tier_state[TIKU_MEM_NVM].initialized = 0;
        }
    }
#endif
    tier_state[TIKU_MEM_NVM].used      = 0;
    tier_state[TIKU_MEM_NVM].peak        = 0;
    tier_state[TIKU_MEM_NVM].alloc_count = 0;
    tier_state[TIKU_MEM_NVM].fail_count  = 0;

#if TIKU_TIER_HIFRAM_AVAILABLE
    tier_state[TIKU_MEM_HIFRAM].buf         = tier_hifram_buf;
    tier_state[TIKU_MEM_HIFRAM].capacity    = TIKU_TIER_HIFRAM_SIZE;
    tier_state[TIKU_MEM_HIFRAM].used      = 0;
    tier_state[TIKU_MEM_HIFRAM].peak        = 0;
    tier_state[TIKU_MEM_HIFRAM].alloc_count = 0;
    tier_state[TIKU_MEM_HIFRAM].fail_count  = 0;
    tier_state[TIKU_MEM_HIFRAM].initialized = 1;
#endif
}

/**
 * @brief Initialize the tier allocator's backing spans.
 *
 * Points each tier at its backing span and zeroes its counters.  Idempotent:
 * a later call returns at once, so a boot-time init followed by a lazy caller
 * cannot orphan live allocations.  The NVM backing is not zeroed.
 *
 * @return TIKU_MEM_OK, or TIKU_MEM_ERR_INVALID from a worker thread or
 *         exception context
 */
tiku_mem_err_t tiku_tier_init(void)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    /* Already wired, for example by a boot-time init before a lazy caller
     * such as BASIC: tiku_tier_reset() is the explicit rewind. */
    if (tier_state[TIKU_MEM_SRAM].initialized) {
        return TIKU_MEM_OK;
    }
    tier_wire_all();
    return TIKU_MEM_OK;
}

/**
 * @brief Reset every tier pool to empty (destructive rewind).
 *
 * Re-wires each tier and zeroes its counters, bypassing init's idempotent
 * guard and orphaning anything already handed out; for teardown and test
 * isolation.  The NVM backing is not zeroed.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_BUSY while a reclaim fence, job or ticket
 *         depends on the current tier identities; TIKU_MEM_ERR_INVALID from a
 *         worker thread or exception context
 */
tiku_mem_err_t tiku_tier_reset(void)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
#if TIKU_MEM_RECLAIM_ENABLE
    if (tiku_reclaim_any_fence() || tiku_reclaim_reset_busy()) return TIKU_MEM_ERR_BUSY;
#endif
    tier_wire_all();
    return TIKU_MEM_OK;
}


/*---------------------------------------------------------------------------*/
/* CHECKED RESERVATIONS                                                      */
/*---------------------------------------------------------------------------*/

/** @brief A fit found in one span, not yet committed to a record. */
typedef struct {
    tier_pool_state_t *span;
    tiku_mem_arch_size_t offset, length, alignment;
    tiku_mem_tier_t tier;
    uint8_t allocation_class;
} work_reservation_t;

/** @brief Round @p size up to @p alignment, a power of two; 0 on overflow. */
static int work_round(tiku_mem_arch_size_t size, tiku_mem_arch_size_t alignment,
                       tiku_mem_arch_size_t *rounded)
{
    const tiku_mem_arch_size_t max = (tiku_mem_arch_size_t)~(tiku_mem_arch_size_t)0;
    const tiku_mem_arch_size_t mask = alignment - 1u;
    if (size > max - mask) return 0;
    *rounded = (size + mask) & ~mask;
    return 1;
}

/**
 * @brief Validate @p options and resolve alignment, flags and allocation
 *        class; 0 when the options are invalid.
 */
static int work_request(const tiku_mem_request_t *options,
        tiku_mem_arch_size_t *alignment, uint16_t *flags, uint8_t *cls)
{
    tiku_mem_class_t c = options ? options->allocation_class : TIKU_MEM_CLASS_DEFAULT;
    *alignment = options ? options->alignment : 0u;
    *flags = options ? options->flags : 0u;
    if ((*flags & ~TIKU_MEM_ALLOW_EXTERNAL) ||
        (*alignment && (*alignment & (*alignment - 1u)))) return 0;
    if (*alignment < TIKU_MEM_ARCH_ALIGNMENT) *alignment = TIKU_MEM_ARCH_ALIGNMENT;
#if TIKU_MEM_RECLAIM_ENABLE
    if (!tiku_reclaim_owner_options(options, cls)) return 0;
    c = (tiku_mem_class_t)*cls;
#else
    /* A feature-disabled build must not silently discard an owner contract. */
    if (options && (options->owner.slot_plus_one || options->owner.generation ||
                    options->owner_slot)) return 0;
    if (c == TIKU_MEM_CLASS_DEFAULT) c = TIKU_MEM_TRANSIENT;
    if (c != TIKU_MEM_TRANSIENT && c != TIKU_MEM_FIXED) return 0;
#endif
    *cls = (uint8_t)c;
    return 1;
}

/**
 * @brief Place @p size bytes in [@p low, @p high) of @p span: FIXED at the
 *        lowest aligned offset, other classes at the highest; 0 if none fits.
 */
static int gap_fit(const tier_pool_state_t *span,
        tiku_mem_arch_size_t low, tiku_mem_arch_size_t high,
        tiku_mem_arch_size_t size, tiku_mem_arch_size_t alignment,
        uint8_t cls, tiku_mem_arch_size_t *offset)
{
    uintptr_t base = (uintptr_t)span->buf, at;
    const uintptr_t mask = alignment - 1u;
    if (high < low || size > high - low || high > UINTPTR_MAX - base) return 0;
    if (cls == TIKU_MEM_FIXED) {
        if (base + low > UINTPTR_MAX - mask) return 0;
        at = (base + low + mask) & ~mask;
        if (at > base + high || size > base + high - at) return 0;
    } else {
        at = (base + high - size) & ~mask;
        if (at < base + low) return 0;
    }
    *offset = (tiku_mem_arch_size_t)(at - base);
    return 1;
}

/**
 * @brief Find room in one span: FIXED takes the lowest gap below every other
 *        class, other classes the highest gap above every FIXED reservation.
 */
static int span_fit(tier_pool_state_t *span, tiku_mem_arch_size_t size,
        tiku_mem_arch_size_t alignment, uint8_t cls, work_reservation_t *out)
{
    tiku_mem_arch_size_t at = 0, boundary = 0, ceiling, offset;
    const backing_record_t *r;
    unsigned i;
    int found = 0;
    if (!span->initialized || span->buf == NULL) return 0;
    /* Fixed reservations grow from the bottom; upper reservations never
     * consume holes below the highest fixed reservation. */
    ceiling = span->capacity;
    for (i = 0; i < TIKU_MEM_MAX_RESERVATIONS; i++) {
        r = &reservations[i];
        if (!r->state || r->span != span) continue;
        if (r->allocation_class == TIKU_MEM_FIXED) {
            if (r->offset + r->length > boundary) boundary = r->offset + r->length;
        } else if (r->offset < ceiling) ceiling = r->offset;
    }
    if (cls != TIKU_MEM_FIXED) { at = boundary; ceiling = span->capacity; }
    for (;;) {
        tiku_mem_arch_size_t end;
        r = record_next(span, at);
        end = r != NULL && r->offset < ceiling ? r->offset : ceiling;
        if (gap_fit(span, at, end, size, alignment, cls, &offset)) {
            out->span = span;
            out->offset = offset;
            out->length = size;
            out->alignment = alignment;
            out->allocation_class = cls;
            found = 1;
            if (cls == TIKU_MEM_FIXED) return 1;
        }
        if (r == NULL || r->offset >= ceiling) break;
        at = r->offset + r->length;
    }
    return found;
}

/**
 * @brief Fit into a span of @p tier (only span @p index when it is not
 *        negative), taking the fullest span that fits; fenced spans are
 *        skipped.
 */
static int work_fit(tiku_mem_tier_t tier, int index,
        tiku_mem_arch_size_t size, tiku_mem_arch_size_t alignment,
        uint8_t cls, work_reservation_t *out)
{
    tier_pool_state_t *span;
    tiku_mem_arch_size_t best_room = 0;
    uint8_t i;
    int found = 0;
    for (i = 0; (span = tier_span(tier, i)) != NULL; i++) {
        work_reservation_t candidate;
        tiku_mem_arch_size_t room = span->capacity - span->used;
#if TIKU_MEM_RECLAIM_ENABLE
        if (span->fenced) continue;
#endif
        if ((index >= 0 && i != index) || (found && room >= best_room)) continue;
        if (span_fit(span, size, alignment, cls, &candidate)) {
            *out = candidate;
            out->tier = tier;
            best_room = room;
            found = 1;
        }
    }
    return found;
}

/**
 * @brief Fit a request into @p tier; for AUTO try HIFRAM first when the
 *        request reaches TIKU_TIER_AUTO_HIFRAM_THRESHOLD, then SRAM, HIFRAM,
 *        and PSRAM only with TIKU_MEM_ALLOW_EXTERNAL.
 */
static int work_select(tiku_mem_tier_t tier, int span_index,
        tiku_mem_arch_size_t size, tiku_mem_arch_size_t policy_size,
        tiku_mem_arch_size_t alignment, uint16_t flags, uint8_t cls,
        work_reservation_t *out)
{
    if (tier != TIKU_MEM_AUTO) return work_fit(tier, span_index, size, alignment, cls, out);
#if TIKU_TIER_HIFRAM_AVAILABLE && TIKU_TIER_AUTO_HIFRAM_THRESHOLD > 0
    if (policy_size >= TIKU_TIER_AUTO_HIFRAM_THRESHOLD &&
        work_fit(TIKU_MEM_HIFRAM, -1, size, alignment, cls, out)) return 1;
#else
    (void)policy_size;
#endif
    if (work_fit(TIKU_MEM_SRAM, -1, size, alignment, cls, out)) return 1;
#if TIKU_TIER_HIFRAM_AVAILABLE
    if (work_fit(TIKU_MEM_HIFRAM, -1, size, alignment, cls, out)) return 1;
#endif
    return (flags & TIKU_MEM_ALLOW_EXTERNAL) &&
           work_fit(TIKU_MEM_PSRAM, -1, size, alignment, cls, out);
}

#if TIKU_MEM_RECLAIM_ENABLE
/**
 * @brief 1 when a fenced span could have taken the request, so the caller
 *        reports BUSY rather than NOMEM.
 */
static int work_fence_blocks(tiku_mem_tier_t tier, int index,
        tiku_mem_arch_size_t length, tiku_mem_arch_size_t alignment,
        uint16_t flags, uint8_t cls)
{
    unsigned ti;
    for (ti = 0; ti < TIKU_MEM_TIER_COUNT; ti++) {
        tier_pool_state_t *span;
        uint8_t si;
        if (tier != TIKU_MEM_AUTO && ti != (unsigned)tier) continue;
        if (tier == TIKU_MEM_AUTO && ti != TIKU_MEM_SRAM && ti != TIKU_MEM_HIFRAM &&
            !(ti == TIKU_MEM_PSRAM && (flags & TIKU_MEM_ALLOW_EXTERNAL))) continue;
        for (si = 0; (span = tier_span((tiku_mem_tier_t)ti, si)) != NULL; si++) {
            work_reservation_t w;
            if ((index < 0 || si == index) && span->fenced &&
                span_fit(span, length, alignment, cls, &w)) return 1;
        }
    }
    return 0;
}
#endif

/**
 * @brief Commit fit @p w to record @p slot with a new generation, charge its
 *        span and return the handle.
 */
static tiku_mem_backing_t work_commit(int slot, const work_reservation_t *w,
        const void *descriptor, uint8_t kind, tiku_mem_arch_size_t stride,
        tiku_mem_arch_size_t count)
{
    backing_record_t *r = &reservations[slot];
    r->generation++;
    r->descriptor = descriptor;
    r->span = w->span;
    r->offset = w->offset;
    r->length = w->length;
    r->alignment = w->alignment;
    r->allocation_class = w->allocation_class;
    r->owner = (tiku_mem_owner_t){0};
    r->owner_slot = 0;
    r->block_size = stride;
    r->block_count = count;
    r->kind = kind;
    r->tier = (uint8_t)w->tier;
    r->state = 1;
    w->span->used += w->length;
    return (tiku_mem_backing_t){r->generation, (uint16_t)(slot + 1)};
}

/**
 * @brief Argument checks shared by the creators: INVALID for bad arguments
 *        or request flags outside a working-memory create, BUSY when
 *        @p descriptor already backs a reservation.
 */
static tiku_mem_err_t create_check(void *descriptor, tiku_mem_tier_t tier,
        int span_index, const tiku_mem_request_t *options, int working,
        tiku_mem_arch_size_t *alignment, uint16_t *flags, uint8_t *cls)
{
    if (descriptor == NULL || (unsigned)tier >= TIKU_MEM_TIER_COUNT ||
        (span_index >= 0 && tier_span(tier, (uint8_t)span_index) == NULL) ||
        !work_request(options, alignment, flags, cls))
        return TIKU_MEM_ERR_INVALID;
    if (!working && *flags != 0u) return TIKU_MEM_ERR_INVALID;
    if (tiku_backing_busy(descriptor)) return TIKU_MEM_ERR_BUSY;
    (void)cls;
    return TIKU_MEM_OK;
}

/**
 * @brief Reserve an interval and publish an arena over it; @p working marks
 *        the tiku_mem_* creators, which alone may pass request flags.
 */
static tiku_mem_err_t arena_create(tiku_arena_t *arena,
        tiku_mem_tier_t tier, int span_index, tiku_mem_arch_size_t size,
        uint8_t id, const tiku_mem_request_t *options, int working)
{
    tiku_arena_t ready = {0};
    work_reservation_t w;
    tiku_mem_arch_size_t alignment, capacity;
    uint16_t flags;
    uint8_t cls;
    int slot;
    tiku_mem_err_t err;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (size == 0u) return TIKU_MEM_ERR_INVALID;
    err = create_check(arena, tier, span_index, options, working, &alignment, &flags, &cls);
    if (err != TIKU_MEM_OK) return err;
    if (!work_round(size, TIKU_MEM_ARCH_ALIGNMENT, &capacity))
        return TIKU_MEM_ERR_NOMEM;
#if TIKU_MEM_RECLAIM_ENABLE
    {
        int claimed = 0;
        err = tiku_reclaim_owner_create(options, tier, span_index,
            TIKU_BACKING_ARENA, capacity, alignment, 0, 0, arena, id, &claimed);
        if (err != TIKU_MEM_OK || claimed) return err;
    }
#endif
    if (!work_select(tier, span_index, capacity, size, alignment, flags, cls, &w)) {
        tier_state[tier == TIKU_MEM_AUTO ? TIKU_MEM_SRAM : tier].fail_count++;
        return
#if TIKU_MEM_RECLAIM_ENABLE
            work_fence_blocks(tier, span_index, capacity, alignment, flags, cls) ? TIKU_MEM_ERR_BUSY :
#endif
            TIKU_MEM_ERR_NOMEM;
    }
    slot = record_available();
    if (slot < 0) return TIKU_MEM_ERR_FULL;
    ready.buf = w.span->buf + w.offset;
    ready.capacity = capacity;
    ready.id = id;
    ready.active = 1;
    ready.tier = w.tier;
    ready.backing = work_commit(slot, &w, arena, TIKU_BACKING_ARENA, 0, 0);
    if (options) {
        reservations[slot].owner = options->owner;
        reservations[slot].owner_slot = options->owner_slot;
    }
    w.span->alloc_count++;
    tier_note_peak(w.span);
    *arena = ready;
    return TIKU_MEM_OK;
}

/**
 * @brief Reserve an interval and build a pool over it; the pool is published
 *        only once its freelist is written.
 */
static tiku_mem_err_t tier_pool_create(tiku_pool_t *pool,
        tiku_mem_tier_t tier, tiku_mem_arch_size_t block_size,
        tiku_mem_arch_size_t count, uint8_t id,
        const tiku_mem_request_t *options, int working)
{
    const tiku_mem_arch_size_t max = (tiku_mem_arch_size_t)~(tiku_mem_arch_size_t)0;
    tiku_mem_arch_size_t alignment, stride, total;
    uint16_t flags;
    uint8_t cls;
    int slot;
    work_reservation_t w;
    tiku_pool_t ready = {0};
    tiku_mem_err_t err;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!block_size || !count) return TIKU_MEM_ERR_INVALID;
    err = create_check(pool, tier, -1, options, working, &alignment, &flags, &cls);
    if (err != TIKU_MEM_OK) return err;
    if (alignment < __alignof__(void *)) alignment = __alignof__(void *);
    if (block_size < sizeof(void *)) block_size = sizeof(void *);
    if (!work_round(block_size, alignment, &stride) || stride > max / count)
        return TIKU_MEM_ERR_NOMEM;
    total = stride * count;
#if TIKU_MEM_RECLAIM_ENABLE
    {
        int claimed = 0;
        err = tiku_reclaim_owner_create(options, tier, -1,
            TIKU_BACKING_POOL, total, alignment, stride, count, pool, id, &claimed);
        if (err != TIKU_MEM_OK || claimed) return err;
    }
#endif
    if (!work_select(tier, -1, total, total, alignment, flags, cls, &w)) {
        tier_state[tier == TIKU_MEM_AUTO ? TIKU_MEM_SRAM : tier].fail_count++;
        return
#if TIKU_MEM_RECLAIM_ENABLE
            work_fence_blocks(tier, -1, total, alignment, flags, cls) ? TIKU_MEM_ERR_BUSY :
#endif
            TIKU_MEM_ERR_NOMEM;
    }
    slot = record_available();
    if (slot < 0) return TIKU_MEM_ERR_FULL;
    /* Reserve before a backend write, publish the descriptor only on success.
     * No pool reset or other NVM write occurs during metadata release. */
    {
        tiku_mem_backing_t h = work_commit(slot, &w, pool, TIKU_BACKING_POOL, stride, count);
        if (options) {
            reservations[slot].owner = options->owner;
            reservations[slot].owner_slot = options->owner_slot;
        }
        /* Initialization, not a usable object yet. */
        reservations[slot].state = 3;
        err = w.tier == TIKU_MEM_NVM ?
            tiku_pool_create_nvm(&ready, w.span->buf + w.offset, stride, count, id) :
            tiku_pool_create(&ready, w.span->buf + w.offset, stride, count, id);
        if (err != TIKU_MEM_OK) {
            w.span->used -= w.length;
            reservations[slot].state = 0;
            return err;
        }
        ready.backing = h;
        reservations[slot].state = 1;
    }
    ready.tier = w.tier;
    w.span->alloc_count++;
    tier_note_peak(w.span);
    *pool = ready;
    return TIKU_MEM_OK;
}

tiku_mem_err_t tiku_mem_arena_create(tiku_arena_t *arena,
        tiku_mem_arch_size_t size, const tiku_mem_request_t *options)
{ return arena_create(arena, TIKU_MEM_AUTO, -1, size, 0, options, 1); }

tiku_mem_err_t tiku_mem_workspace_open(tiku_mem_workspace_t *workspace,
        tiku_mem_arch_size_t size, const tiku_mem_request_t *options)
{ return tiku_mem_arena_create(workspace, size, options); }

tiku_mem_err_t tiku_mem_pool_create(tiku_pool_t *pool,
        tiku_mem_arch_size_t size, tiku_mem_arch_size_t count,
        const tiku_mem_request_t *options)
{ return tier_pool_create(pool, TIKU_MEM_AUTO, size, count, 0, options, 1); }

tiku_mem_err_t tiku_tier_arena_create_opts(tiku_arena_t *arena,
        tiku_mem_tier_t tier, tiku_mem_arch_size_t size, uint8_t id,
        const tiku_mem_request_t *options)
{ return arena_create(arena, tier, -1, size, id, options, 0); }

tiku_mem_err_t tiku_tier_arena_create_span_opts(tiku_arena_t *arena,
        tiku_mem_tier_t tier, uint8_t span_index, tiku_mem_arch_size_t size,
        uint8_t id, const tiku_mem_request_t *options)
{ return arena_create(arena, tier, span_index, size, id, options, 0); }

tiku_mem_err_t tiku_tier_pool_create_opts(tiku_pool_t *pool,
        tiku_mem_tier_t tier, tiku_mem_arch_size_t size, tiku_mem_arch_size_t count,
        uint8_t id, const tiku_mem_request_t *options)
{ return tier_pool_create(pool, tier, size, count, id, options, 0); }

tiku_mem_err_t tiku_tier_arena_create(tiku_arena_t *arena,
        tiku_mem_tier_t tier, tiku_mem_arch_size_t size, uint8_t id)
{ return tiku_tier_arena_create_opts(arena, tier, size, id, NULL); }

tiku_mem_err_t tiku_tier_arena_create_span(tiku_arena_t *arena,
        tiku_mem_tier_t tier, uint8_t span_index, tiku_mem_arch_size_t size,
        tiku_mem_arch_size_t alignment, uint8_t id)
{
    tiku_mem_request_t options = TIKU_MEM_REQUEST_DEFAULT;
    options.alignment = alignment;
    return tiku_tier_arena_create_span_opts(arena, tier, span_index, size, id, &options);
}

tiku_mem_err_t tiku_tier_pool_create(tiku_pool_t *pool,
        tiku_mem_tier_t tier, tiku_mem_arch_size_t size, tiku_mem_arch_size_t count,
        uint8_t id)
{ return tiku_tier_pool_create_opts(pool, tier, size, count, id, NULL); }

#if TIKU_MEM_RECLAIM_ENABLE
/* Private coordinator seam. Holds have identities but no published descriptor.
 * Credits consume metadata only, never bytes in a physical span. */
int tiku_reclaim_record(unsigned slot, tiku_reclaim_record_t *out)
{
    const backing_record_t *r;
    uint8_t si = 0;
    if (slot >= TIKU_MEM_MAX_RESERVATIONS || out == NULL) return 0;
    r = &reservations[slot];
    if (!r->state || r->state == 4) return 0;
    while (tier_span((tiku_mem_tier_t)r->tier, si) != r->span) si++;
    *out = (tiku_reclaim_record_t){0};
    out->descriptor = r->descriptor;
    out->handle = (tiku_mem_backing_t){r->generation, (uint16_t)(slot + 1)};
    out->owner = r->owner; out->owner_slot = r->owner_slot;
    out->tier = (tiku_mem_tier_t)r->tier; out->span_index = si;
    out->kind = r->kind; out->allocation_class = r->allocation_class;
    out->state = r->state; out->offset = r->offset; out->length = r->length;
    out->alignment = r->alignment;
    out->block_size = r->block_size; out->block_count = r->block_count;
    return 1;
}

tiku_mem_err_t tiku_reclaim_normalize(const tiku_mem_reclaim_request_t *request,
                                      tiku_reclaim_record_t *out)
{
    tiku_mem_arch_size_t align, size, stride = 0, count = 0;
    const tiku_mem_arch_size_t max = (tiku_mem_arch_size_t)~(tiku_mem_arch_size_t)0;
    uint16_t flags;
    uint8_t cls;
    if (request == NULL || out == NULL || !request->size ||
        !work_request(&request->options, &align, &flags, &cls) ||
        (unsigned)request->placement > TIKU_MEM_PLACE_SPAN)
        return TIKU_MEM_ERR_INVALID;
    if (request->placement != TIKU_MEM_PLACE_WORKING &&
        (flags || request->tier == TIKU_MEM_AUTO ||
         tier_span(request->tier, request->placement == TIKU_MEM_PLACE_SPAN ?
                   request->span_index : 0) == NULL)) return TIKU_MEM_ERR_INVALID;
    if (request->kind == TIKU_MEM_REQUEST_ARENA) {
        if (request->count) return TIKU_MEM_ERR_INVALID;
        if (!work_round(request->size, TIKU_MEM_ARCH_ALIGNMENT, &size))
            return TIKU_MEM_ERR_NOMEM;
    } else if (request->kind == TIKU_MEM_REQUEST_POOL) {
        if (!request->count) return TIKU_MEM_ERR_INVALID;
        if (align < __alignof__(void *)) align = __alignof__(void *);
        stride = request->size < sizeof(void *) ? sizeof(void *) : request->size;
        if (!work_round(stride, align, &stride) || stride > max / request->count)
            return TIKU_MEM_ERR_NOMEM;
        count = request->count; size = stride * count;
    } else return TIKU_MEM_ERR_INVALID;
    *out = (tiku_reclaim_record_t){0};
    out->kind = (uint8_t)request->kind; out->length = size;
    out->alignment = align; out->block_size = stride; out->block_count = count;
    out->allocation_class = cls; out->owner = request->options.owner;
    out->owner_slot = request->options.owner_slot;
    return TIKU_MEM_OK;
}

tiku_mem_err_t tiku_reclaim_hold_direct(tiku_reclaim_record_t *record,
                                        const tiku_mem_reclaim_request_t *request)
{
    work_reservation_t w;
    tiku_mem_tier_t tier = request->placement == TIKU_MEM_PLACE_WORKING ?
                          TIKU_MEM_AUTO : request->tier;
    int slot, si = request->placement == TIKU_MEM_PLACE_SPAN ? request->span_index : -1;
    if (!work_select(tier, si, record->length,
                     request->kind == TIKU_MEM_REQUEST_POOL ? record->length : request->size,
                     record->alignment,
                     request->options.flags, record->allocation_class, &w))
        return TIKU_MEM_ERR_NOMEM;
    slot = record_available();
    if (slot < 0) return TIKU_MEM_ERR_FULL;
    (void)work_commit(slot, &w, NULL, record->kind, record->block_size, record->block_count);
    reservations[slot].owner = record->owner;
    reservations[slot].owner_slot = record->owner_slot;
    reservations[slot].state = 2;
    tier_note_peak(w.span);
    (void)tiku_reclaim_record((unsigned)slot, record);
    return TIKU_MEM_OK;
}

/**
 * @brief Record a held handle names (state 2), or a credit (state 4) when
 *        @p credit is set; NULL otherwise.
 */
static backing_record_t *held_record(tiku_mem_backing_t h, int credit)
{
    backing_record_t *r;
    if (!h.slot_plus_one || h.slot_plus_one > TIKU_MEM_MAX_RESERVATIONS) return NULL;
    r = &reservations[h.slot_plus_one - 1u];
    return r->generation == h.generation && h.generation &&
           (r->state == 2 || (credit && r->state == 4)) ? r : NULL;
}

int tiku_reclaim_credit(tiku_mem_backing_t *handle)
{
    int slot = record_available();
    backing_record_t *r;
    if (slot < 0) return 0;
    r = &reservations[slot];
    r->generation++; r->state = 4; r->span = NULL; r->descriptor = NULL;
    *handle = (tiku_mem_backing_t){r->generation, (uint16_t)(slot + 1)};
    return 1;
}

void tiku_reclaim_drop(tiku_mem_backing_t handle)
{
    backing_record_t *r = held_record(handle, 1);
    if (!r) return;
    if (r->state == 2) r->span->used -= r->length;
    r->state = 0;
}

tiku_mem_err_t tiku_reclaim_bind(tiku_mem_backing_t h, void *descriptor, uint8_t id, int restoring)
{
    backing_record_t *r = held_record(h, 0);
    tiku_mem_err_t err;
    if (!r || !descriptor || !r->span->initialized) return TIKU_MEM_ERR_INVALID;
    if (tiku_backing_busy(descriptor)) return TIKU_MEM_ERR_BUSY;
    if (r->kind == TIKU_BACKING_ARENA) {
        tiku_arena_t a = {0};
        a.buf = r->span->buf + r->offset; a.capacity = r->length;
        a.active = 1; a.id = id; a.tier = (tiku_mem_tier_t)r->tier; a.backing = h;
        *(tiku_arena_t *)descriptor = a;
    } else {
        tiku_pool_t p = {0};
        err = r->tier == TIKU_MEM_NVM ?
            tiku_pool_create_nvm(&p, r->span->buf + r->offset,
                                 r->block_size, r->block_count, id) :
            tiku_pool_create(&p, r->span->buf + r->offset,
                             r->block_size, r->block_count, id);
        if (err != TIKU_MEM_OK) return err;
        p.backing = h; p.tier = (tiku_mem_tier_t)r->tier;
        *(tiku_pool_t *)descriptor = p;
    }
    r->descriptor = descriptor; r->state = 1;
    if (!restoring) r->span->alloc_count++;
    return TIKU_MEM_OK;
}

int tiku_reclaim_fenced(tiku_mem_tier_t tier, uint8_t si)
{
    tier_pool_state_t *span = tier_span(tier, si);
    return span && span->fenced;
}
int tiku_reclaim_fence(tiku_mem_tier_t tier, uint8_t si, int enabled)
{
    tier_pool_state_t *span = tier_span(tier, si);
    if (!span || !span->initialized) return 0;
    span->fenced = (uint8_t)(enabled != 0);
    return 1;
}
int tiku_reclaim_any_fence(void)
{
    unsigned t;
    for (t = 0; t < TIKU_MEM_TIER_COUNT; t++) {
        tier_pool_state_t *span; uint8_t si;
        for (si = 0; (span = tier_span((tiku_mem_tier_t)t, si)) != NULL; si++)
            if (span->fenced) return 1;
    }
    return 0;
}

/**
 * @brief 1 when @p planned names a credit whose interval lies inside its
 *        fenced span, so install_hold() may place it.
 */
static int publish_hold(const tiku_reclaim_record_t *planned)
{
    backing_record_t *r = held_record(planned->handle, 1);
    tier_pool_state_t *span = tier_span(planned->tier, planned->span_index);
    if (!r || r->state != 4 || !span || !span->initialized || !span->fenced ||
        !planned->length || planned->offset > span->capacity ||
        planned->length > span->capacity - planned->offset) return 0;
    return 1;
}
/** @brief Turn the credit @p planned names into a hold charged to its span. */
static void install_hold(const tiku_reclaim_record_t *planned)
{
    backing_record_t *r = &reservations[planned->handle.slot_plus_one - 1u];
    r->span = tier_span(planned->tier, planned->span_index);
    r->offset = planned->offset; r->length = planned->length;
    r->alignment = planned->alignment; r->allocation_class = planned->allocation_class;
    r->owner = planned->owner; r->owner_slot = planned->owner_slot;
    r->kind = planned->kind; r->tier = (uint8_t)planned->tier;
    r->block_size = planned->block_size; r->block_count = planned->block_count;
    r->descriptor = NULL; r->state = 2;
    r->span->used += r->length; tier_note_peak(r->span);
}
int tiku_reclaim_publish(const tiku_reclaim_record_t *old,
                         tiku_reclaim_record_t *replacement, unsigned count,
                         tiku_reclaim_record_t *request)
{
    unsigned i;
    /* Full validation precedes every mutation. No callbacks, buffer writes,
     * pool resets, hardware operations or persistence inside this
     * transition. */
    if (!publish_hold(request)) return 0;
    for (i = 0; i < count; i++) {
        backing_record_t *r = record_get(old[i].descriptor, old[i].handle, old[i].kind);
        if (!r || !r->span->fenced || !publish_hold(&replacement[i]) ||
            r->allocation_class != TIKU_MEM_RESTARTABLE ||
            !tiku_backing_valid(old[i].descriptor, old[i].handle, old[i].kind)) return 0;
    }
    tiku_atomic_enter();
    for (i = 0; i < count; i++) {
        backing_record_t *r = &reservations[old[i].handle.slot_plus_one - 1u];
        if (r->kind == TIKU_BACKING_ARENA)
            ((tiku_arena_t *)r->descriptor)->active = 0;
        else ((tiku_pool_t *)r->descriptor)->active = 0;
        r->span->used -= r->length; r->state = 0;
    }
    for (i = 0; i < count; i++) install_hold(&replacement[i]);
    install_hold(request);
    tiku_atomic_exit();
    return 1;
}

/* An exited process cannot resume its old protothread. On pre-commit abort,
 * end just that owner's old lifetimes and turn its reserved credits into holds
 * at exactly the original addresses, for the restarted owner to bind. Other
 * prepared owners keep their live objects. */
int tiku_reclaim_restore_original(const tiku_reclaim_record_t *old,
    tiku_reclaim_record_t *replacement, unsigned count, tiku_mem_owner_t owner)
{
    unsigned i;
    for (i = 0; i < count; i++) {
        backing_record_t *r;
        if (old[i].owner.slot_plus_one != owner.slot_plus_one ||
            old[i].owner.generation != owner.generation) continue;
        r = record_get(old[i].descriptor, old[i].handle, old[i].kind);
        if (!r || !r->span->fenced || !publish_hold(&replacement[i]) ||
            !tiku_backing_valid(old[i].descriptor, old[i].handle, old[i].kind)) return 0;
    }
    tiku_atomic_enter();
    for (i = 0; i < count; i++) {
        backing_record_t *r;
        if (old[i].owner.slot_plus_one != owner.slot_plus_one ||
            old[i].owner.generation != owner.generation) continue;
        r = &reservations[old[i].handle.slot_plus_one - 1u];
        if (r->kind == TIKU_BACKING_ARENA)
            ((tiku_arena_t *)r->descriptor)->active = 0;
        else ((tiku_pool_t *)r->descriptor)->active = 0;
        r->span->used -= r->length; r->state = 0;
        replacement[i].offset = old[i].offset;
        install_hold(&replacement[i]);
    }
    tiku_atomic_exit();
    return 1;
}

/* A failed initializer may have claimed only some slots. After its cleanup
 * contract establishes stopped access, turn those claims back into holds.
 * Refuse generation exhaustion before touching any descriptor or record. */
int tiku_reclaim_rearm(tiku_reclaim_record_t *layout, unsigned count,
                       tiku_mem_owner_t owner)
{
    unsigned i;
    for (i = 0; i < count; i++) {
        backing_record_t *r;
        if (layout[i].owner.slot_plus_one != owner.slot_plus_one ||
            layout[i].owner.generation != owner.generation) continue;
        if (!layout[i].handle.slot_plus_one ||
            layout[i].handle.slot_plus_one > TIKU_MEM_MAX_RESERVATIONS) return 0;
        r = &reservations[layout[i].handle.slot_plus_one - 1u];
        if (r->generation != layout[i].handle.generation || !r->span ||
            !r->span->fenced || (r->state != 1 && r->state != 2) ||
            (r->state == 1 && (r->generation == TIKU_MEM_GENERATION_MAX ||
             !tiku_backing_valid(r->descriptor, layout[i].handle, r->kind)))) return 0;
    }
    tiku_atomic_enter();
    for (i = 0; i < count; i++) {
        backing_record_t *r;
        if (layout[i].owner.slot_plus_one != owner.slot_plus_one ||
            layout[i].owner.generation != owner.generation) continue;
        r = &reservations[layout[i].handle.slot_plus_one - 1u];
        if (r->state == 2) continue;
        if (r->kind == TIKU_BACKING_ARENA)
            ((tiku_arena_t *)r->descriptor)->active = 0;
        else ((tiku_pool_t *)r->descriptor)->active = 0;
        r->generation++; r->state = 2; r->descriptor = NULL;
        layout[i].handle.generation = r->generation;
    }
    tiku_atomic_exit();
    return 1;
}

tiku_mem_err_t tiku_reclaim_context_tag(tiku_proc_mem_t *pmem,
                                        tiku_mem_owner_t owner, uint16_t key_base)
{
    tiku_arena_t *arenas[3] = {&pmem->sram_arena, &pmem->nvm_arena, &pmem->hifram_arena};
    backing_record_t *records[3] = {0};
    unsigned i, count = 0;
    if (!tiku_mem_owner_available(owner)) return TIKU_MEM_ERR_BUSY;
    for (i = 0; i < 3; i++) {
        tiku_mem_request_t options = TIKU_MEM_REQUEST_DEFAULT;
        tiku_mem_err_t err; int claimed = 0;
        if (!arenas[i]->active) continue;
        records[i] = record_get(arenas[i], arenas[i]->backing, TIKU_BACKING_ARENA);
        if (!records[i] || !tiku_backing_valid(arenas[i], arenas[i]->backing, TIKU_BACKING_ARENA) ||
            records[i]->allocation_class != TIKU_MEM_TRANSIENT ||
            records[i]->owner.slot_plus_one) return TIKU_MEM_ERR_INVALID;
        if (records[i]->span->fenced) return TIKU_MEM_ERR_BUSY;
        options.owner = owner; options.owner_slot = (uint16_t)(key_base + i);
        err = tiku_reclaim_owner_create(&options, (tiku_mem_tier_t)records[i]->tier,
            -1, TIKU_BACKING_ARENA, records[i]->length, records[i]->alignment,
            0, 0, arenas[i], arenas[i]->id, &claimed);
        if (err != TIKU_MEM_OK || claimed) return err == TIKU_MEM_OK ? TIKU_MEM_ERR_INVALID : err;
        count++;
    }
    if (!count) return TIKU_MEM_ERR_INVALID;
    tiku_atomic_enter();
    for (i = 0; i < 3; i++) if (records[i]) {
        records[i]->owner = owner;
        records[i]->owner_slot = (uint16_t)(key_base + i);
        records[i]->allocation_class = TIKU_MEM_RESTARTABLE;
    }
    pmem->owner = owner; pmem->owner_key_base = key_base;
    tiku_atomic_exit();
    return TIKU_MEM_OK;
}
#endif

/*---------------------------------------------------------------------------*/
/* TIER QUERY                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Query which memory tier a pointer belongs to.
 *
 * Scans the tier backing pools first, which also works on host where the static
 * arrays may not appear in the region table, then falls back to the region
 * registry so plain static buffers still classify.  Containment uses uintptr_t.
 *
 * @param ptr       Address to query (must be non-NULL)
 * @param out_tier  Output: tier of the containing pool/region
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on NULL
 *         arguments, TIKU_MEM_ERR_NOT_FOUND if the address is not in
 *         any tier pool or tier-mappable region
 */
tiku_mem_err_t tiku_tier_get(const uint8_t *ptr,
                              tiku_mem_tier_t *out_tier)
{
    int i;
    uintptr_t addr;

    if (ptr == NULL || out_tier == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    addr = (uintptr_t)ptr;

    /* Every span of every concrete tier (SRAM, NVM, HIFRAM, PSRAM); AUTO
     * has no backing of its own. */
    for (i = 0; i < TIKU_MEM_TIER_COUNT; i++) {
        uint8_t si;
        tier_pool_state_t *ts;
        if (i == TIKU_MEM_AUTO) {
            continue;
        }
        for (si = 0; (ts = tier_span((tiku_mem_tier_t)i, si)) != NULL; si++) {
            uintptr_t pool_start = (uintptr_t)ts->buf;

            if (ts->initialized && addr >= pool_start &&
                (addr - pool_start) < (uintptr_t)ts->capacity) {
                *out_tier = (tiku_mem_tier_t)i;
                return TIKU_MEM_OK;
            }
        }
    }

    /* Fall back to region registry for non-tier-managed memory */
    {
        tiku_mem_region_type_t region_type;
        tiku_mem_err_t err;

        err = tiku_region_get_type(ptr, &region_type);
        if (err == TIKU_MEM_OK) {
            switch (region_type) {
            case TIKU_MEM_REGION_SRAM:
                *out_tier = TIKU_MEM_SRAM;
                return TIKU_MEM_OK;
            case TIKU_MEM_REGION_NVM:
                *out_tier = TIKU_MEM_NVM;
                return TIKU_MEM_OK;
            default:
                break;
            }
        }
    }

    return TIKU_MEM_ERR_NOT_FOUND;
}

/*---------------------------------------------------------------------------*/
/* TIER STATS                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Fill a stats struct with a tier backing pool's current state.
 *
 * Whole-tier occupancy rather than one arena's: capacity, bytes handed out, the
 * lifetime peak and the sub-allocation count.  AUTO has no pool and is
 * rejected, as is a concrete tier that was never initialised.
 *
 * @param tier   Memory tier to query (SRAM, NVM, HIFRAM or PSRAM; not AUTO)
 * @param stats  Output statistics (must be non-NULL)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if stats is
 *         NULL, tier is AUTO/out of range, or the tier is uninitialized
 */
tiku_mem_err_t tiku_tier_stats(tiku_mem_tier_t tier,
                                tiku_mem_stats_t *stats)
{
    const tier_pool_state_t *ts;

    if (stats == NULL || tier == TIKU_MEM_AUTO) {
        return TIKU_MEM_ERR_INVALID;
    }

    if (tier != TIKU_MEM_SRAM &&
        tier != TIKU_MEM_NVM &&
        tier != TIKU_MEM_HIFRAM &&
        tier != TIKU_MEM_PSRAM) {
        return TIKU_MEM_ERR_INVALID;
    }

    ts = &tier_state[tier];
    if (!ts->initialized) {
        /* HIFRAM on a build without it, PSRAM before it is attached, or a
         * tier not yet initialised: all report "not available" alike. */
        return TIKU_MEM_ERR_INVALID;
    }

    stats->total_bytes = ts->capacity;
    stats->used_bytes  = ts->used;
    stats->peak_bytes  = ts->peak;
    stats->alloc_count = ts->alloc_count;
    stats->fail_count  = ts->fail_count;
#if defined(TIKU_TIER_SRAM_EXTRA)
    if (tier == TIKU_MEM_SRAM) {
        stats->total_bytes += sram_extra.capacity;
        stats->used_bytes += sram_extra.used;
        stats->alloc_count += sram_extra.alloc_count;
        stats->fail_count += sram_extra.fail_count;
        stats->peak_bytes = sram_peak;
    }
#endif

    return TIKU_MEM_OK;
}

tiku_mem_err_t tiku_tier_span_stats(tiku_mem_tier_t tier, uint8_t index,
                                    const uint8_t **base,
                                    tiku_mem_stats_t *stats)
{
    const tier_pool_state_t *ts = tier_span(tier, index);
    if (base == NULL || stats == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }
    if (ts == NULL || !ts->initialized) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }
    *base = ts->buf;
    stats->total_bytes = ts->capacity;
    stats->used_bytes = ts->used;
    stats->peak_bytes = ts->peak;
    stats->alloc_count = ts->alloc_count;
    stats->fail_count = ts->fail_count;
    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* NVM-TIER WRITE                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write into NVM-tier memory through its backing path.
 *
 * NVM-tier memory reads by plain pointer, but a write inside the carved
 * region goes through the region backend, which programs the medium; MSP430's
 * FRAM array takes a store.  Opens and closes the NVM window itself.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID on a NULL or out-of-range write,
 *         or from a worker thread or exception context; TIKU_MEM_ERR_IO when
 *         the backend write or the relock flush fails
 */
tiku_mem_err_t tiku_tier_nvm_write(void *dst, const void *src,
                                   tiku_mem_arch_size_t len)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (dst == NULL || src == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }
    /* A backend owns only its validated address range. */
    {
        const tiku_nvm_backend_t *rgn = tiku_nvm_backend_get();

        if (rgn != NULL && rgn->base != NULL && rgn->write != NULL) {
            uintptr_t d = (uintptr_t)dst;
            uintptr_t b = (uintptr_t)rgn->base;
            if (d >= b && (d - b) <= rgn->size &&
                (size_t)len <= rgn->size - (size_t)(d - b)) {
                uint16_t mpu = tiku_mpu_unlock_nvm();
                int rc = rgn->write((tiku_nvm_backend_t *)rgn,
                                    (size_t)(d - b), src, (size_t)len);
                if (rc != 0) {
                    /* Restore protection without another program attempt. */
                    tiku_mpu_arch_lock_nvm(mpu);
                    return TIKU_MEM_ERR_IO;
                }
                return tiku_mpu_lock_nvm_status(mpu);
            }
        }
    }

#ifdef PLATFORM_MSP430
    /* The lower-FRAM tier is a separate owner and needs an MPU window. */
    {
        uintptr_t d = (uintptr_t)dst;
        uintptr_t b = (uintptr_t)tier_nvm_buf;
        if (d < b || (d - b) > sizeof tier_nvm_buf ||
            (size_t)len > sizeof tier_nvm_buf - (size_t)(d - b)) {
            return TIKU_MEM_ERR_INVALID;
        }
        uint16_t mpu = tiku_mpu_unlock_nvm();
        tiku_mem_arch_nvm_write(dst, src, len);
        return tiku_mpu_lock_nvm_status(mpu);
    }
#else
    /* Outside any region, and no MSP430 FRAM array: there is nowhere durable
     * for the write to go. */
    (void)len;
    return TIKU_MEM_ERR_INVALID;
#endif
}
