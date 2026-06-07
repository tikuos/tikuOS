/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tier.c - Tier-aware memory allocator
 *
 * Provides placement-aware allocation by managing pre-allocated
 * backing pools for SRAM and NVM. The caller specifies a memory
 * tier (SRAM, NVM, or AUTO) and the tier allocator carves the
 * buffer from the correct physical memory type, then initializes
 * a standard arena or pool over it.
 *
 * Why tier-aware allocation:
 *   On MCUs with both SRAM and NVM (FRAM), the caller often knows
 *   whether data is hot (frequent reads/writes, volatile OK) or
 *   cold (rarely updated, persistence desired). The tier allocator
 *   makes this intent explicit without requiring the caller to
 *   manage raw buffers and memory regions manually.
 *
 * Why AUTO:
 *   AUTO prefers SRAM (fast, low-energy) and falls back to NVM
 *   when SRAM is exhausted. This gives a simple "best effort"
 *   placement without the caller needing to know the memory budget.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_mem.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Round a size up to the platform's required alignment
 *
 * Uses TIKU_MEM_ARCH_ALIGNMENT (provided by the memory HAL) so the
 * same code works across 16-bit, 32-bit, and 64-bit targets. Every
 * sub-buffer carved from a tier pool is sized through this helper so
 * that each returned bump pointer lands on a natural boundary.
 *
 * The standard power-of-two round-up formula:
 *   (size + (align - 1)) & ~(align - 1)
 *
 * Example (alignment = 4): 3 -> 4, 4 -> 4, 5 -> 8, 0 -> 0
 *
 * @param size  Raw size in bytes
 * @return Size rounded up to TIKU_MEM_ARCH_ALIGNMENT boundary
 */
static tiku_mem_arch_size_t align_up(tiku_mem_arch_size_t size)
{
    const tiku_mem_arch_size_t mask = TIKU_MEM_ARCH_ALIGNMENT - 1U;
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
static uint8_t __attribute__((aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_sram_buf[TIKU_TIER_SRAM_SIZE];

/**
 * @brief Backing store for the NVM tier
 *
 * On MSP430 this is forced into the non-volatile .persistent (FRAM)
 * section and zero-initialized, so its contents survive reset. On host
 * builds (no PLATFORM_MSP430) it falls back to ordinary .bss, which is
 * sufficient for tests that exercise the tier-routing logic. Sized by
 * TIKU_TIER_NVM_SIZE (default 1024 bytes).
 */
#ifdef PLATFORM_MSP430
static uint8_t __attribute__((section(".persistent"),
                              aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_nvm_buf[TIKU_TIER_NVM_SIZE] = {0};
#else
static uint8_t __attribute__((aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_nvm_buf[TIKU_TIER_NVM_SIZE];
#endif

/**
 * @brief Backing store for the HIFRAM (upper FRAM bank) tier
 *
 * HIFRAM tier backing pool — only declared when both:
 *   1. The device has an upper FRAM bank (FR5994, FR6989), and
 *   2. The build is using MEMORY_MODEL=large (set by the Makefile
 *      via -DTIKU_MEMORY_MODEL_LARGE=1).
 *
 * Both gates are required: TIKU_HIFRAM_BSS expands to a section
 * attribute that targets .upper.bss, which only exists as an output
 * section under -mdata-region=either. Declaring this array in a
 * small-mode build would link-fail with "no .upper.bss output".
 *
 * On host builds and on chip variants without HIFRAM, the array
 * doesn't exist and TIKU_TIER_HIFRAM_AVAILABLE stays 0; the tier
 * APIs that touch HIFRAM return TIKU_MEM_ERR_NOMEM gracefully.
 *
 * Sized by TIKU_TIER_HIFRAM_SIZE (default 32 KB). The size type is
 * 16-bit on MSP430, so this pool cannot exceed 64 KB without widening
 * tiku_mem_arch_size_t.
 */
#if defined(TIKU_DEVICE_HAS_HIFRAM) && TIKU_DEVICE_HAS_HIFRAM && \
    defined(TIKU_MEMORY_MODEL_LARGE) && TIKU_MEMORY_MODEL_LARGE
TIKU_HIFRAM_BSS
static uint8_t __attribute__((aligned(TIKU_MEM_ARCH_ALIGNMENT)))
    tier_hifram_buf[TIKU_TIER_HIFRAM_SIZE];
/**
 * @brief Compile-time flag: 1 when the HIFRAM tier pool exists
 *
 * Set to 1 on the HIFRAM + large-model path above, 0 everywhere else.
 * Guards every site that names tier_hifram_buf or initializes the
 * TIKU_MEM_HIFRAM slot of tier_state[], so a build without HIFRAM
 * never references the (absent) array.
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
    uint8_t               initialized; /**< Non-zero after tiku_tier_init */
} tier_pool_state_t;

/**
 * @brief Per-tier bump-allocator state, indexed by tiku_mem_tier_t
 *
 * Indexed by tiku_mem_tier_t. Slot 2 (AUTO) is unused at runtime —
 * AUTO is resolved to a concrete tier before any indexing happens —
 * but we leave the slot in the array so the enum-to-index mapping
 * stays direct. Slot 3 (HIFRAM) is initialized only when
 * TIKU_TIER_HIFRAM_AVAILABLE is set.
 *
 * Sized to TIKU_MEM_TIER_COUNT (4: SRAM, NVM, AUTO, HIFRAM). Module
 * scope so every tier_* function shares one allocator state. Lives in
 * .bss, so all fields (including the per-slot initialized flag) start
 * at zero before tiku_tier_init() runs.
 */
static tier_pool_state_t tier_state[TIKU_MEM_TIER_COUNT];

/*---------------------------------------------------------------------------*/
/* TIER INIT                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the tier allocator's backing pools
 *
 * Points each tier's bump-allocator state at its static backing array
 * and resets the offset, peak, and allocation counters to zero. Must
 * be called once after tiku_mem_init() (which sets up the region
 * registry and MPU) and before any tiku_tier_* allocation call.
 *
 * The SRAM and NVM tiers are always initialized. The HIFRAM tier slot
 * is only wired up when TIKU_TIER_HIFRAM_AVAILABLE is set (device has an
 * upper FRAM bank AND the build is MEMORY_MODEL=large); otherwise its
 * initialized flag stays zero and every HIFRAM path degrades to a clean
 * "not available". The AUTO slot is deliberately left untouched — AUTO
 * is resolved to a concrete tier before tier_state[] is ever indexed.
 *
 * Idempotent in effect but destructive: calling it again rewinds all
 * bump pointers, orphaning any sub-buffers already handed out. It does
 * not zero the NVM backing array, so persistent FRAM contents survive.
 *
 * @return TIKU_MEM_OK (always succeeds)
 */
tiku_mem_err_t tiku_tier_init(void)
{
    tier_state[TIKU_MEM_SRAM].buf         = tier_sram_buf;
    tier_state[TIKU_MEM_SRAM].capacity    = TIKU_TIER_SRAM_SIZE;
    tier_state[TIKU_MEM_SRAM].offset      = 0;
    tier_state[TIKU_MEM_SRAM].peak        = 0;
    tier_state[TIKU_MEM_SRAM].alloc_count = 0;
    tier_state[TIKU_MEM_SRAM].initialized = 1;

    tier_state[TIKU_MEM_NVM].buf         = tier_nvm_buf;
    tier_state[TIKU_MEM_NVM].capacity    = TIKU_TIER_NVM_SIZE;
    tier_state[TIKU_MEM_NVM].offset      = 0;
    tier_state[TIKU_MEM_NVM].peak        = 0;
    tier_state[TIKU_MEM_NVM].alloc_count = 0;
    tier_state[TIKU_MEM_NVM].initialized = 1;

#if TIKU_TIER_HIFRAM_AVAILABLE
    tier_state[TIKU_MEM_HIFRAM].buf         = tier_hifram_buf;
    tier_state[TIKU_MEM_HIFRAM].capacity    = TIKU_TIER_HIFRAM_SIZE;
    tier_state[TIKU_MEM_HIFRAM].offset      = 0;
    tier_state[TIKU_MEM_HIFRAM].peak        = 0;
    tier_state[TIKU_MEM_HIFRAM].alloc_count = 0;
    tier_state[TIKU_MEM_HIFRAM].initialized = 1;
#endif

    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* RESOLVE AUTO                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Resolve TIKU_MEM_AUTO to a concrete tier
 *
 * Routing policy (in order of preference):
 *
 *   1. **HIFRAM** if available AND size >= TIKU_TIER_AUTO_HIFRAM_THRESHOLD
 *      AND HIFRAM has room. The threshold avoids paying the 20-bit
 *      pointer cost on small objects that fit comfortably in SRAM.
 *
 *   2. **SRAM** if it has room. Fast and low-energy — preferred for
 *      small/hot data.
 *
 *   3. **NVM** as the last resort. Persists across reboots but writes
 *      are slower and require MPU unlock bracketing.
 *
 * SRAM, NVM, and HIFRAM tiers pass through unchanged. The threshold
 * defaults to 1 KB; set TIKU_TIER_AUTO_HIFRAM_THRESHOLD=0 to never
 * route AUTO to HIFRAM. On parts without HIFRAM (TIKU_TIER_HIFRAM_
 * AVAILABLE = 0) the policy degrades to today's SRAM-or-NVM behavior
 * with no observable change.
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
        return NULL;
    }

    if (aligned > ts->capacity - ts->offset) {
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
 * @brief Create an arena backed by the specified memory tier
 *
 * Resolves the requested tier (AUTO is mapped to a concrete tier by
 * resolve_tier()), carves an aligned sub-buffer from that tier's
 * backing pool via tier_bump_alloc(), and initializes the arena
 * control block over it. The resulting arena behaves identically to
 * one made with tiku_arena_create() — only the buffer source differs.
 *
 * The arena struct is populated directly rather than by delegating to
 * tiku_arena_create(); see the inline comment for the two reasons (the
 * tier pool already holds the region claim, and tier_bump_alloc()
 * already returns an aligned base). The recorded tier is the resolved
 * tier, so later introspection (arena->tier, tiku_tier_get()) reflects
 * the physical placement, not the AUTO hint.
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
 * @brief Create a fixed-size block pool backed by the specified tier
 *
 * Computes the exact total buffer size the pool will need — mirroring
 * the per-block alignment and minimum-block-size logic in
 * tiku_pool_create() so the tier carve is neither short nor wasteful —
 * resolves the tier, carves the buffer, then hands it to
 * tiku_pool_create() to lay out the embedded freelist.
 *
 * Side effect (NVM tier): tiku_pool_create() writes next-pointers into
 * every block while building the freelist. When the resolved tier is
 * NVM (FRAM), those writes would fault against MPU NVM write-protection,
 * so this function brackets the create call with
 * tiku_mpu_unlock_nvm() / tiku_mpu_lock_nvm(). On host and for non-NVM
 * tiers the MPU helpers are no-ops, so the unbracketed path is taken.
 * The resolved tier is stamped onto pool->tier on success.
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
     * alignment logic in tiku_pool_create() so we allocate
     * exactly enough space.
     */
    aligned_blk = align_up(block_size);
    min_blk = align_up((tiku_mem_arch_size_t)sizeof(void *));
    if (aligned_blk < min_blk) {
        aligned_blk = min_blk;
    }
    total = aligned_blk * block_count;

    resolved = resolve_tier(tier, total);
    buf = tier_bump_alloc(resolved, total);

    if (buf == NULL) {
        return TIKU_MEM_ERR_NOMEM;
    }

    /*
     * For NVM-backed pools, freelist construction writes next-pointers
     * into FRAM blocks. The MPU must be temporarily unlocked. On host
     * the MPU functions are no-ops, so this is safe everywhere.
     */
    if (resolved == TIKU_MEM_NVM) {
        uint16_t saved = tiku_mpu_unlock_nvm();
        err = tiku_pool_create(pool, buf, block_size, block_count, id);
        tiku_mpu_lock_nvm(saved);
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
 * @brief Query which memory tier a pointer belongs to
 *
 * Two-stage lookup. First it scans the tier allocator's own backing
 * pools (SRAM, NVM, HIFRAM), skipping the AUTO slot which never owns a
 * pool. This stage works on host as well as target, because the static
 * backing arrays may not appear in the platform's region table on host.
 * If the address is not inside any tier pool, it falls back to the
 * region registry (tiku_region_get_type()) and maps the region type to
 * a tier so that non-tier-managed memory (plain static buffers) still
 * classifies.
 *
 * Containment is tested with uintptr_t arithmetic (addr - pool_start
 * compared against capacity) to avoid pointer-comparison UB across
 * distinct objects. The region-registry fallback only resolves SRAM
 * and NVM region types; PERIPHERAL and FLASH regions have no tier and
 * are reported as not found.
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
     * not be in the platform's region table on host. We iterate
     * over every concrete tier (SRAM, NVM, HIFRAM) and skip AUTO
     * since it never has its own backing pool. */
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
 * @brief Fill a stats struct with a tier backing pool's current state
 *
 * Reports the whole-tier high-level usage (not a single arena): total
 * pool capacity, bytes handed out so far, the lifetime high-water mark,
 * and the number of sub-allocations carved from the pool. This is the
 * data the shell and /proc surface for overall tier occupancy.
 *
 * Rejects AUTO (it has no backing pool of its own) and any value that
 * is not one of the three concrete tiers. A concrete tier whose
 * initialized flag is clear — e.g. HIFRAM on a non-HIFRAM or
 * small-model build — is reported as TIKU_MEM_ERR_INVALID, the same
 * "not available" signal used for an as-yet-uninitialized tier.
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

    if (tier != TIKU_MEM_SRAM &&
        tier != TIKU_MEM_NVM &&
        tier != TIKU_MEM_HIFRAM) {
        return TIKU_MEM_ERR_INVALID;
    }

    ts = &tier_state[tier];
    if (!ts->initialized) {
        /* HIFRAM tier on a non-HIFRAM build, or an as-yet-uninited
         * tier — both report "not available" the same way. */
        return TIKU_MEM_ERR_INVALID;
    }

    stats->total_bytes = ts->capacity;
    stats->used_bytes  = ts->offset;
    stats->peak_bytes  = ts->peak;
    stats->alloc_count = ts->alloc_count;

    return TIKU_MEM_OK;
}
