/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tier.c - tier-aware memory allocator.
 *
 * Carves a buffer from the caller's chosen memory type (SRAM, NVM or AUTO) and
 * initialises an arena or pool over it.  AUTO prefers SRAM and falls back to NVM,
 * so a caller can express intent without managing raw buffers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_mem.h"
#include "tiku_nvm_region.h"
#include <stddef.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Round a size up to the platform's required alignment.
 *
 * Uses TIKU_MEM_ARCH_ALIGNMENT from the HAL, so the same code works on 16-, 32-
 * and 64-bit targets.  Every sub-buffer carved from a tier pool goes through
 * it, which is what puts each bump pointer on a natural boundary.
 *
 * @param size  Raw size in bytes
 * @return Size rounded up to TIKU_MEM_ARCH_ALIGNMENT boundary
 */
static tiku_mem_arch_size_t align_up(tiku_mem_arch_size_t size)
{
    const tiku_mem_arch_size_t mask = TIKU_MEM_ARCH_ALIGNMENT - 1U;
    /* Saturate instead of wrapping to 0 on a near-max request (16-bit on
     * MSP430), so the caller's capacity check rejects it cleanly. */
    if (size > (tiku_mem_arch_size_t)(~(tiku_mem_arch_size_t)0 - mask)) {
        return (tiku_mem_arch_size_t)(~(tiku_mem_arch_size_t)0 & ~mask);
    }
    return (size + mask) & ~mask;
}

/*---------------------------------------------------------------------------*/
/* BACKING POOLS                                                             */
/*---------------------------------------------------------------------------*/

/*
 * Static arrays that serve as the backing store for each memory tier.
 * On MSP430, the NVM pool is placed in FRAM via the .persistent
 * section. On host, both pools reside in regular BSS.
 *
 * The caller controls pool sizes via TIKU_TIER_SRAM_SIZE and
 * TIKU_TIER_NVM_SIZE defines (set before including tiku_mem.h).
 * Both arrays are aligned to TIKU_MEM_ARCH_ALIGNMENT so the very
 * first byte handed out by tier_bump_alloc() is already aligned.
 */

/**
 * @brief Backing store for the SRAM tier
 *
 * Resides in regular .bss (volatile SRAM). Sized by
 * TIKU_TIER_SRAM_SIZE (default 128 bytes). tiku_tier_init() points
 * tier_state[TIKU_MEM_SRAM].buf at this array.
 */
#if defined(TIKU_TIER_SRAM_DERIVED)
/* Every ARM part: the linker carves the span from whatever its tier bank has
 * left after the statics (arch/common/tiku_sram_layout.ld -- .bss on
 * RA8P1/RP2350/L15, .ssram on Ambiq, .ram2 on LM20).  No array, so no size
 * to keep in step with the build configuration, and nothing for the crt to
 * zero -- the allocator does not promise zeroed memory. */
extern uint8_t __tier_sram_start;
extern uint8_t __tier_sram_end;
#define TIER_SRAM_BUF  (&__tier_sram_start)
#define TIER_SRAM_CAP  ((tiku_mem_arch_size_t)(&__tier_sram_end - \
                                               &__tier_sram_start))
#else
static uint8_t __attribute__((aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_sram_buf[TIKU_TIER_SRAM_SIZE];
#endif

#ifndef TIER_SRAM_BUF
#define TIER_SRAM_BUF  (tier_sram_buf)
#define TIER_SRAM_CAP  ((tiku_mem_arch_size_t)TIKU_TIER_SRAM_SIZE)
#endif

/**
 * @brief Backing store for the NVM tier, on the one architecture that needs it.
 *
 * MSP430's FRAM is unified with the code estate, so there is no region to
 * carve and TIKU_DURABLE puts this array in genuinely non-volatile memory.
 * Every other board takes its NVM tier from the carved region.
 */
/* No untagged fallback: a tier promising survival across power loss must never
 * quietly be RAM, so a board with neither unified FRAM nor a carved region has
 * NO NVM tier and asking for one fails at the call site. */
#ifdef PLATFORM_MSP430
static TIKU_DURABLE uint8_t __attribute__((aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_nvm_buf[TIKU_TIER_NVM_SIZE] = {0};
#endif

/**
 * @brief Backing store for the HIFRAM (upper FRAM bank) tier.
 *
 * Declared only when the device has an upper bank AND the build is large-model,
 * because the section attribute targets an output section that only exists
 * then.  Elsewhere the array is absent and the HIFRAM paths return NOMEM.
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
 * @brief Per-tier bump allocator state
 *
 * Each tier has a simple bump-pointer allocator over its backing pool.
 * Sub-buffers carved from here become the backing storage for user
 * arenas and pools.
 */
typedef struct {
    uint8_t              *buf;         /**< Backing pool start */
    tiku_mem_arch_size_t  capacity;    /**< Total pool size in bytes */
    tiku_mem_arch_size_t  offset;      /**< Current bump position */
    tiku_mem_arch_size_t  peak;        /**< Lifetime high-water mark */
    tiku_mem_arch_size_t  alloc_count; /**< Number of sub-allocations */
    tiku_mem_arch_size_t  fail_count;  /**< Carves refused for lack of room */
    uint8_t               initialized; /**< Non-zero after tiku_tier_init */
} tier_pool_state_t;

/**
 * @brief Per-tier bump-allocator state, indexed by tiku_mem_tier_t.
 *
 * The AUTO slot is never indexed at runtime -- AUTO resolves to a concrete tier
 * first -- but it stays in the array to keep the enum-to-index mapping direct.
 * In .bss, so every field starts at zero before init runs.
 */
static tier_pool_state_t tier_state[TIKU_MEM_TIER_COUNT];

/*---------------------------------------------------------------------------*/
/* TIER INIT                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the tier allocator's backing pools.
 *
 * Points each tier at its backing array and zeroes its counters.  Idempotent:
 * a later call returns at once, so a boot-time init followed by a lazy caller
 * cannot orphan live allocations.  The NVM backing array is not zeroed.
 *
 * @return TIKU_MEM_OK (always succeeds)
 */
/*
 * Wire every tier pool to its backing array and rewind all bump pointers
 * (offset/peak/alloc_count -> 0).  UNCONDITIONAL and destructive: any
 * sub-arena previously handed out by tiku_tier_alloc/arena_create is
 * orphaned.  Shared by tiku_tier_init() (guarded, once at boot) and
 * tiku_tier_reset() (on demand, teardown / test isolation).  Does not zero
 * the NVM backing array, so persistent FRAM contents survive.
 */
/*---------------------------------------------------------------------------*/
/* PSRAM TIER -- late attach (Apollo510 external PSRAM)                      */
/*---------------------------------------------------------------------------*/

tiku_mem_err_t tiku_tier_attach_psram(void *base, tiku_mem_arch_size_t size)
{
    if (base == NULL || size == 0u) {
        return TIKU_MEM_ERR_INVALID;
    }
    if (tier_state[TIKU_MEM_PSRAM].initialized) {
        return TIKU_MEM_ERR_INVALID;    /* already attached */
    }
    tier_state[TIKU_MEM_PSRAM].buf         = (uint8_t *)base;
    tier_state[TIKU_MEM_PSRAM].capacity    = size;
    tier_state[TIKU_MEM_PSRAM].offset      = 0;
    tier_state[TIKU_MEM_PSRAM].peak        = 0;
    tier_state[TIKU_MEM_PSRAM].alloc_count = 0;
    tier_state[TIKU_MEM_PSRAM].fail_count  = 0;
    tier_state[TIKU_MEM_PSRAM].initialized = 1;
    return TIKU_MEM_OK;
}

tiku_mem_err_t tiku_tier_detach_psram(int force)
{
    if (!tier_state[TIKU_MEM_PSRAM].initialized) {
        return TIKU_MEM_OK;             /* already gone: idempotent */
    }
    if (tier_state[TIKU_MEM_PSRAM].offset != 0u && !force) {
        return TIKU_MEM_ERR_INVALID;    /* live allocations would be stranded */
    }
    tier_state[TIKU_MEM_PSRAM].initialized = 0;
#if defined(TIKU_TIER_POISON)
    /*
     * On the parts whose tier the linker carves, the span sits outside the
     * crt's zero loop, so its contents are whatever the last boot left.  The
     * allocator does not promise zeroed memory: tiku_arena_alloc() has no
     * memset and the NVM tier backing is documented unzeroed.  Filling with a
     * value no caller could mistake for zero makes a caller that reads before
     * it writes fail here rather than in the field.
     */
    {
        size_t pi;
        uint8_t *pb = TIER_SRAM_BUF;
        for (pi = 0; pi < (size_t)TIER_SRAM_CAP; pi++) {
            pb[pi] = 0xA5u;
        }
    }
#endif

    tier_state[TIKU_MEM_PSRAM].buf         = NULL;
    tier_state[TIKU_MEM_PSRAM].capacity    = 0;
    tier_state[TIKU_MEM_PSRAM].offset      = 0;
    return TIKU_MEM_OK;
}

static void tier_wire_all(void)
{
    /* PSRAM: never wired at boot -- it is a LATE-ATTACH tier owned by the
     * PSRAM lifecycle (tiku_tier_attach_psram).  A tiku_tier_reset() drops
     * any attachment, which is correct: reset means clean slate. */
    tier_state[TIKU_MEM_PSRAM].initialized = 0;
    tier_state[TIKU_MEM_PSRAM].buf         = NULL;
    tier_state[TIKU_MEM_PSRAM].capacity    = 0;
    tier_state[TIKU_MEM_PSRAM].offset      = 0;

    tier_state[TIKU_MEM_SRAM].buf         = TIER_SRAM_BUF;
    tier_state[TIKU_MEM_SRAM].capacity    = TIER_SRAM_CAP;
    tier_state[TIKU_MEM_SRAM].offset      = 0;
    tier_state[TIKU_MEM_SRAM].peak        = 0;
    tier_state[TIKU_MEM_SRAM].alloc_count = 0;
    tier_state[TIKU_MEM_SRAM].initialized = 1;

#ifdef PLATFORM_MSP430
    /* Unified FRAM: the pool above is the NVM, and it is really non-volatile. */
    tier_state[TIKU_MEM_NVM].buf         = tier_nvm_buf;
    tier_state[TIKU_MEM_NVM].capacity    = TIKU_TIER_NVM_SIZE;
    tier_state[TIKU_MEM_NVM].initialized = 1;
#else
    {
        /* Asked, not listed.  tiku_nvm_backend_get() is the one authority on
         * whether this board carved a region -- its weak default returns NULL
         * where no arch backend exists -- so a new port needs no entry
         * anywhere: it gets a working NVM tier the moment it supplies a
         * backend, and an honestly absent one until then.
         *
         * The tier owns a FIXED 32 KB extent at the front (the portable
         * allocation contract); the file store absorbs the remainder. */
        const tiku_nvm_backend_t *rgn = tiku_nvm_backend_get();

        if (rgn != NULL && rgn->base != NULL &&
            rgn->size > (size_t)TIKU_NVM_TIER_BYTES) {
            tier_state[TIKU_MEM_NVM].buf      = rgn->base;
            tier_state[TIKU_MEM_NVM].capacity =
                (tiku_mem_arch_size_t)TIKU_NVM_TIER_BYTES;
            tier_state[TIKU_MEM_NVM].initialized = 1;
        } else {
            /* NOT initialized, rather than an empty-but-present tier: the
             * difference is what makes tiku_tier_arena_create(TIKU_MEM_NVM)
             * fail at the call site instead of handing back memory that does
             * not have the property the caller asked for. */
            tier_state[TIKU_MEM_NVM].buf         = NULL;
            tier_state[TIKU_MEM_NVM].capacity    = 0u;
            tier_state[TIKU_MEM_NVM].initialized = 0;
        }
    }
#endif
    tier_state[TIKU_MEM_NVM].offset      = 0;
    tier_state[TIKU_MEM_NVM].peak        = 0;
    tier_state[TIKU_MEM_NVM].alloc_count = 0;

#if TIKU_TIER_HIFRAM_AVAILABLE
    tier_state[TIKU_MEM_HIFRAM].buf         = tier_hifram_buf;
    tier_state[TIKU_MEM_HIFRAM].capacity    = TIKU_TIER_HIFRAM_SIZE;
    tier_state[TIKU_MEM_HIFRAM].offset      = 0;
    tier_state[TIKU_MEM_HIFRAM].peak        = 0;
    tier_state[TIKU_MEM_HIFRAM].alloc_count = 0;
    tier_state[TIKU_MEM_HIFRAM].initialized = 1;
#endif
}

tiku_mem_err_t tiku_tier_init(void)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    /* Idempotent guard: skip the (destructive) rewind if already set up, so
     * a boot-time init followed by a lazy caller (e.g. BASIC) does not
     * orphan live allocations.  tiku_tier_reset() is the explicit rewind. */
    if (tier_state[TIKU_MEM_SRAM].initialized) {
        return TIKU_MEM_OK;
    }
    tier_wire_all();
    return TIKU_MEM_OK;
}

/**
 * @brief Reset every tier pool to empty (destructive rewind).
 *
 * Re-wires each tier and zeroes its counters unconditionally, bypassing the
 * idempotent guard in init and orphaning anything already handed out -- so this
 * is for teardown and test isolation.  The NVM backing array is not zeroed.
 *
 * @return TIKU_MEM_OK (always succeeds)
 */
tiku_mem_err_t tiku_tier_reset(void)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    tier_wire_all();
    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* RESOLVE AUTO                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Resolve TIKU_MEM_AUTO to a concrete tier.
 *
 * Prefers HIFRAM when it is available, has room, and the request meets the
 * threshold -- which keeps small objects off the 20-bit pointer path -- then
 * SRAM, then NVM.  Concrete tiers pass through unchanged.
 */
static tiku_mem_tier_t resolve_tier(tiku_mem_tier_t tier,
                                     tiku_mem_arch_size_t size)
{
    tiku_mem_arch_size_t aligned;

    if (tier != TIKU_MEM_AUTO) {
        return tier;
    }

    aligned = align_up(size);

#if TIKU_TIER_HIFRAM_AVAILABLE
    /* Route bulk allocations to HIFRAM if the threshold is met and
     * the HIFRAM tier has room. This is the main AUTO win on
     * FR5994/FR6989: a 16 KB ML feature table no longer competes
     * with the kernel's 4-8 KB SRAM budget. */
    if (TIKU_TIER_AUTO_HIFRAM_THRESHOLD > 0 &&
        size >= TIKU_TIER_AUTO_HIFRAM_THRESHOLD &&
        tier_state[TIKU_MEM_HIFRAM].initialized &&
        aligned <= tier_state[TIKU_MEM_HIFRAM].capacity -
                   tier_state[TIKU_MEM_HIFRAM].offset) {
        return TIKU_MEM_HIFRAM;
    }
#endif

    /* Prefer SRAM if it has room; fall back to NVM */
    if (tier_state[TIKU_MEM_SRAM].initialized &&
        aligned <= tier_state[TIKU_MEM_SRAM].capacity -
                   tier_state[TIKU_MEM_SRAM].offset) {
        return TIKU_MEM_SRAM;
    }

#if TIKU_TIER_HIFRAM_AVAILABLE
    /* Last-resort capacity check: NVM full but HIFRAM has room (a
     * sub-threshold size that no longer fits anywhere else).  Without
     * it AUTO would return NVM unconditionally and fail the carve with
     * HIFRAM capacity sitting idle. */
    if (tier_state[TIKU_MEM_NVM].initialized &&
        aligned > tier_state[TIKU_MEM_NVM].capacity -
                  tier_state[TIKU_MEM_NVM].offset &&
        tier_state[TIKU_MEM_HIFRAM].initialized &&
        aligned <= tier_state[TIKU_MEM_HIFRAM].capacity -
                   tier_state[TIKU_MEM_HIFRAM].offset) {
        return TIKU_MEM_HIFRAM;
    }
#endif

    return TIKU_MEM_NVM;
}

/*---------------------------------------------------------------------------*/
/* BUMP ALLOCATE FROM TIER POOL                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Carve a sub-buffer from a tier's backing pool
 *
 * Simple bump-pointer allocation. Returns an aligned pointer or
 * NULL if the tier is not initialized or lacks capacity.
 *
 * @param tier  Concrete tier (must not be AUTO)
 * @param size  Bytes needed (will be aligned up)
 * @return Pointer to the sub-buffer, or NULL
 */
static uint8_t *tier_bump_alloc(tiku_mem_tier_t tier,
                                 tiku_mem_arch_size_t size)
{
    tier_pool_state_t *ts = &tier_state[tier];
    tiku_mem_arch_size_t aligned = align_up(size);
    uint8_t *ptr;

    if (!ts->initialized) {
        ts->fail_count++;
        return NULL;
    }

    if (aligned > ts->capacity - ts->offset) {
        ts->fail_count++;
        return NULL;
    }

    ptr = ts->buf + ts->offset;
    ts->offset += aligned;
    ts->alloc_count++;

    if (ts->offset > ts->peak) {
        ts->peak = ts->offset;
    }

    return ptr;
}

/*---------------------------------------------------------------------------*/
/* TIER ARENA CREATE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Create an arena backed by the specified memory tier.
 *
 * Resolves the tier, carves an aligned sub-buffer from its pool, and builds the
 * arena over it -- otherwise identical to an ordinary arena.  The RESOLVED tier
 * is recorded, so later introspection shows physical placement, not the hint.
 *
 * @param arena  Arena control block to initialize
 * @param tier   Memory tier hint (SRAM, NVM, HIFRAM, or AUTO)
 * @param size   Desired arena capacity in bytes (must be > 0)
 * @param id     User-assigned identifier for debugging (0-255)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad
 *         arguments, TIKU_MEM_ERR_NOMEM if the resolved tier lacks
 *         room (or is not initialized, e.g. HIFRAM on a small build)
 */
tiku_mem_err_t tiku_tier_arena_create(tiku_arena_t *arena,
                                       tiku_mem_tier_t tier,
                                       tiku_mem_arch_size_t size,
                                       uint8_t id)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    tiku_mem_tier_t resolved;
    tiku_mem_arch_size_t aligned;
    uint8_t *buf;

    if (arena == NULL || size == 0) {
        return TIKU_MEM_ERR_INVALID;
    }

    resolved = resolve_tier(tier, size);
    buf = tier_bump_alloc(resolved, size);

    if (buf == NULL) {
        return TIKU_MEM_ERR_NOMEM;
    }

    /*
     * Initialize the arena struct directly rather than calling
     * tiku_arena_create(). Reasons:
     *
     * 1. The backing pool's memory is managed by the tier allocator,
     *    not by individual arenas. Calling tiku_arena_create() would
     *    invoke tiku_region_claim() on a sub-range that overlaps the
     *    tier pool's own claim, which the region registry rejects.
     *
     * 2. The bump pointer from tier_bump_alloc() is already aligned,
     *    so no base-alignment adjustment is needed.
     */
    aligned = align_up(size);

    arena->buf      = buf;
    arena->capacity = aligned;
    arena->offset   = 0;
    arena->peak     = 0;
    arena->count    = 0;
    arena->id       = id;
    arena->active   = 1;
    arena->tier     = resolved;

    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* TIER POOL CREATE                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Create a fixed-size block pool backed by the specified tier.
 *
 * Computes the exact buffer the pool needs, mirroring the pool's own alignment
 * and minimum-block rules so the carve is neither short nor wasteful.  On the
 * NVM tier the create is bracketed by the unlock window, since it writes links.
 *
 * @param pool         Pool control block to initialize
 * @param tier         Memory tier hint (SRAM, NVM, HIFRAM, or AUTO)
 * @param block_size   Requested size of each block in bytes (must be > 0)
 * @param block_count  Number of blocks (must be > 0)
 * @param id           User-assigned identifier for debugging (0-255)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad
 *         arguments, TIKU_MEM_ERR_NOMEM if the resolved tier lacks room
 */
tiku_mem_err_t tiku_tier_pool_create(tiku_pool_t *pool,
                                      tiku_mem_tier_t tier,
                                      tiku_mem_arch_size_t block_size,
                                      tiku_mem_arch_size_t block_count,
                                      uint8_t id)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    tiku_mem_tier_t resolved;
    tiku_mem_arch_size_t aligned_blk;
    tiku_mem_arch_size_t min_blk;
    tiku_mem_arch_size_t total;
    uint8_t *buf;
    tiku_mem_err_t err;

    if (pool == NULL || block_size == 0 || block_count == 0) {
        return TIKU_MEM_ERR_INVALID;
    }

    /*
     * Compute total buffer size needed. This must mirror the
     * alignment logic in tiku_pool_create() so the allocation is
     * exactly big enough.
     */
    aligned_blk = align_up(block_size);
    min_blk = align_up((tiku_mem_arch_size_t)sizeof(void *));
    if (aligned_blk < min_blk) {
        aligned_blk = min_blk;
    }
    /* Reject before the product wraps (tiku_mem_arch_size_t is 16-bit on
     * MSP430): a wrapped 'total' reserves ~0 bytes while build_freelist writes
     * the full block_size*block_count span, corrupting the tier pool. Cast to
     * the unsigned type BEFORE dividing so the bound is unsigned, not -1/N. */
    {
        const tiku_mem_arch_size_t size_max =
            (tiku_mem_arch_size_t)~(tiku_mem_arch_size_t)0;
        if (block_count != 0u && aligned_blk > size_max / block_count) {
            return TIKU_MEM_ERR_NOMEM;
        }
    }
    total = aligned_blk * block_count;

    resolved = resolve_tier(tier, total);
    buf = tier_bump_alloc(resolved, total);

    if (buf == NULL) {
        return TIKU_MEM_ERR_NOMEM;
    }

    /*
     * NVM-tier pools build and maintain their embedded freelist through
     * tiku_tier_nvm_write() -- the bootrom program op on MRAM, the flash program
     * on RP2350, an in-place store on FRAM (a direct CPU store would bus-fault on
     * program-op NVM, which is what crash-looped this before).
     * tiku_pool_create_nvm() marks the pool so every freelist write takes that
     * path; the write primitive brackets its own NVM-unlock window, so no outer
     * unlock is needed here. SRAM pools store directly (the hot path).
     */
    if (resolved == TIKU_MEM_NVM) {
        err = tiku_pool_create_nvm(pool, buf, block_size, block_count, id);
    } else {
        err = tiku_pool_create(pool, buf, block_size, block_count, id);
    }

    if (err == TIKU_MEM_OK) {
        pool->tier = resolved;
    }

    return err;
}

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

    /* Check the tier allocator's own backing pools first.
     * This works on both host and target — the backing pools may
     * not be in the platform's region table on host. The loop covers
     * every concrete tier (SRAM, NVM, HIFRAM) and skips AUTO, which
     * never has its own backing pool. */
    for (i = 0; i < TIKU_MEM_TIER_COUNT; i++) {
        if (i == TIKU_MEM_AUTO) {
            continue;
        }
        if (tier_state[i].initialized) {
            uintptr_t pool_start = (uintptr_t)tier_state[i].buf;

            if (addr >= pool_start &&
                (addr - pool_start) <
                    (uintptr_t)tier_state[i].capacity) {
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
 * @param tier   Memory tier to query (SRAM, NVM, or HIFRAM; not AUTO)
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

    /* PSRAM belongs here as much as the others.  It was added as a tier in
     * M4 and this whitelist was not updated with it, which made a 64 MB
     * attached tier INVISIBLE to every stats consumer -- `free` simply did
     * not mention it, and read as though the memory were not there.  A tier
     * the allocator honours but the accounting cannot see is worse than one
     * that does not exist, because nothing looks wrong. */
    if (tier != TIKU_MEM_SRAM &&
        tier != TIKU_MEM_NVM &&
        tier != TIKU_MEM_HIFRAM &&
        tier != TIKU_MEM_PSRAM) {
        return TIKU_MEM_ERR_INVALID;
    }

    ts = &tier_state[tier];
    if (!ts->initialized) {
        /* HIFRAM on a non-HIFRAM build, PSRAM before `power psram up`, or an
         * as-yet-uninited tier — all report "not available" the same way. */
        return TIKU_MEM_ERR_INVALID;
    }

    stats->total_bytes = ts->capacity;
    stats->used_bytes  = ts->offset;
    stats->peak_bytes  = ts->peak;
    stats->alloc_count = ts->alloc_count;
    stats->fail_count  = ts->fail_count;

    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* NVM-TIER WRITE (backend-aware)                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write into NVM-tier memory through the correct backing path.
 *
 * NVM-tier memory reads by plain pointer, but writing differs: a mapped MRAM
 * region is programmed by the bootrom, since a CPU store would fault against
 * its read-only mapping, while FRAM is byte-writable.  Brackets the unlock itself.
 *
 * @return TIKU_MEM_OK, or TIKU_MEM_ERR_INVALID on a NULL or out-of-range write.
 */
tiku_mem_err_t tiku_tier_nvm_write(void *dst, const void *src,
                                   tiku_mem_arch_size_t len)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (dst == NULL || src == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }
    /* Ask the backend, do not consult a platform list: if this board carved a
     * region its writes go through the program path, and the same code is
     * correct on a board that has not carved one. */
    {
        const tiku_nvm_backend_t *rgn = tiku_nvm_backend_get();

        if (rgn != NULL && rgn->base != NULL && rgn->write != NULL) {
            uintptr_t d = (uintptr_t)dst;
            uintptr_t b = (uintptr_t)rgn->base;
            if (d < b || (size_t)(d - b) > rgn->size ||
                (size_t)len > rgn->size - (size_t)(d - b)) {
                return TIKU_MEM_ERR_INVALID;     /* dst not in the region */
            }
            {
                uint16_t mpu = tiku_mpu_unlock_nvm();
                int rc = rgn->write((tiku_nvm_backend_t *)rgn,
                                    (size_t)(d - b), src, (size_t)len);
                tiku_mpu_lock_nvm(mpu);
                return (rc == 0) ? TIKU_MEM_OK : TIKU_MEM_ERR_INVALID;
            }
        }
        if (rgn != NULL) {
            /* A region exists but its backend cannot write: a direct CPU store
             * would bus-fault on program-op NVM, so fail rather than fall
             * through to the in-place copy below. */
            return TIKU_MEM_ERR_INVALID;
        }
    }

#ifdef PLATFORM_MSP430
    /* Unified FRAM: byte-writable in place, and the only architecture where an
     * NVM-tier write is a plain store. */
    {
        uint16_t mpu = tiku_mpu_unlock_nvm();
        memcpy(dst, src, (size_t)len);
        tiku_mpu_lock_nvm(mpu);
    }
    return TIKU_MEM_OK;
#else
    /* No region and no unified FRAM: this board has no NVM tier, so there is
     * nowhere for this write to go.  Returning OK here would report a durable
     * write that never happened. */
    (void)len;
    return TIKU_MEM_ERR_INVALID;
#endif
}
