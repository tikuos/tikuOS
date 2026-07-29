/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem.c - Arena allocator implementation
 *
 * Implements the arena (bump-pointer) allocator for fragmentation-free
 * memory management on microcontrollers with small SRAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_mem.h"
#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Round a size up to the platform's required alignment.
 *
 * Uses TIKU_MEM_ARCH_ALIGNMENT from the memory HAL, so the same code works on
 * 16-, 32- and 64-bit targets.
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
/* ARENA FUNCTIONS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize an arena without region-registry validation.
 *
 * For library code needing an arena over an embedded struct member before the
 * region registry exists.  The arena is marked SRAM tier; every other operation
 * behaves identically.
 *
 * @param arena    Arena control block to initialize
 * @param buf      Pointer to the backing buffer
 * @param size     Size of the backing buffer in bytes
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad arguments
 */
tiku_mem_err_t tiku_arena_create_raw(tiku_arena_t *arena, uint8_t *buf,
                                      tiku_mem_arch_size_t size)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (arena == NULL || buf == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Align the buffer base up to the platform's required alignment. */
    {
        uintptr_t raw     = (uintptr_t)buf;
        uintptr_t mask    = (uintptr_t)(TIKU_MEM_ARCH_ALIGNMENT - 1U);
        uintptr_t aligned = (raw + mask) & ~mask;
        tiku_mem_arch_size_t adj = (tiku_mem_arch_size_t)(aligned - raw);

        if (size <= adj) {       /* misaligned base leaves no usable span */
            return TIKU_MEM_ERR_INVALID;
        }
        arena->buf      = (uint8_t *)aligned;
        arena->capacity = size - adj;
    }
    arena->offset   = 0;
    arena->peak     = 0;
    arena->fail     = 0;
    arena->count    = 0;
    arena->id       = 0;
    arena->active   = 1;
    arena->tier     = TIKU_MEM_SRAM;

    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize an arena over a caller-provided buffer
 *
 * The arena does not own or allocate the buffer — the caller provides
 * a static array or a section of SRAM. This avoids any dependency on
 * a heap allocator.
 *
 * @param arena    Arena control block to initialize
 * @param buf      Pointer to the backing buffer
 * @param size     Size of the backing buffer in bytes
 * @param id       User-assigned identifier for debugging
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad arguments
 */
tiku_mem_err_t tiku_arena_create(tiku_arena_t *arena, uint8_t *buf,
                                 tiku_mem_arch_size_t size, uint8_t id)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (arena == NULL || buf == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Determine which memory tier the buffer resides in.
     * Both SRAM and NVM are valid backing stores — the tier is
     * recorded so callers can introspect placement later. */
    if (tiku_region_contains(buf, size, TIKU_MEM_REGION_SRAM)) {
        arena->tier = TIKU_MEM_SRAM;
    } else if (tiku_region_contains(buf, size, TIKU_MEM_REGION_NVM)) {
        arena->tier = TIKU_MEM_NVM;
    } else {
        return TIKU_MEM_ERR_INVALID;
    }
    tiku_region_claim(buf, size, id);

    /* Align the buffer base up to the platform's required alignment.
     * The claim covers the original range; the arena uses the aligned
     * subset so every returned pointer is naturally aligned. */
    {
        uintptr_t raw     = (uintptr_t)buf;
        uintptr_t mask    = (uintptr_t)(TIKU_MEM_ARCH_ALIGNMENT - 1U);
        uintptr_t aligned = (raw + mask) & ~mask;
        tiku_mem_arch_size_t adj = (tiku_mem_arch_size_t)(aligned - raw);

        if (size <= adj) {       /* misaligned base leaves no usable span */
            return TIKU_MEM_ERR_INVALID;
        }
        arena->buf      = (uint8_t *)aligned;
        arena->capacity = size - adj;
    }
    arena->offset   = 0;
    arena->peak     = 0;
    arena->fail     = 0;
    arena->count    = 0;
    arena->id       = id;
    arena->active   = 1;

    return TIKU_MEM_OK;
}

/**
 * @brief Allocate memory from an arena (bump-pointer).
 *
 * Advances the offset by the aligned size and tracks the peak for high-water
 * reporting.  There is no individual free: dropping it removes per-object
 * metadata, free-list walks and fragmentation, which small SRAM cannot afford.
 *
 * @param arena    Arena to allocate from (must be active)
 * @param size     Bytes requested (must be > 0)
 * @return Aligned pointer, or NULL on failure
 */
void *tiku_arena_alloc(tiku_arena_t *arena, tiku_mem_arch_size_t size)
{
    TIKU_MEM_KERNEL_ONLY(NULL);
    tiku_mem_arch_size_t aligned;
    void *ptr;

    if (arena == NULL || !arena->active || size == 0) {
        return NULL;
    }

    aligned = align_up(size);

    /* Check for overflow: would the new offset exceed capacity? */
    if (aligned > arena->capacity - arena->offset) {
        arena->fail++;
        return NULL;
    }

    ptr = &arena->buf[arena->offset];
    arena->offset += aligned;

    /* Track lifetime high-water mark */
    if (arena->offset > arena->peak) {
        arena->peak = arena->offset;
    }

    arena->count++;

    return ptr;
}

/**
 * @brief Reset an arena, reclaiming all allocations at once.
 *
 * Sets the offset back to zero.  The buffer is NOT zeroed -- that would make
 * reset O(n) and defeat a bump allocator -- so callers must not assume it is.
 * The peak high-water mark survives, so it stays a lifetime maximum.
 *
 * @param arena    Arena to reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if arena is NULL
 */
tiku_mem_err_t tiku_arena_reset(tiku_arena_t *arena)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (arena == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    arena->offset = 0;
    arena->count  = 0;

    return TIKU_MEM_OK;
}

/**
 * @brief Securely reset an arena, zeroing all memory before reclaiming.
 *
 * Delegates the zeroing to the arch layer, which uses a volatile access so the
 * compiler cannot optimise it away, then resets the arena state.
 */
tiku_mem_err_t tiku_arena_secure_reset(tiku_arena_t *arena)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (arena == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Delegate to the arch layer for a platform-optimized secure wipe. */
    tiku_mem_arch_secure_wipe(arena->buf, arena->capacity);

    arena->offset = 0;
    arena->count  = 0;

    return TIKU_MEM_OK;
}

/**
 * @brief Fill a stats struct with the arena's current state
 *
 * @param arena    Arena to query
 * @param stats    Output structure
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if either is NULL
 */
tiku_mem_err_t tiku_arena_stats(const tiku_arena_t *arena,
                                tiku_mem_stats_t *stats)
{
    if (arena == NULL || stats == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }

    stats->total_bytes = arena->capacity;
    stats->used_bytes  = arena->offset;
    stats->peak_bytes  = arena->peak;
    stats->alloc_count = arena->count;
    stats->fail_count  = arena->fail;

    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* MODULE INIT                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the memory management module.
 *
 * The region registry goes first, because arena and persist registrations
 * validate their buffers against it.  Then MPU NVM write-protection is armed,
 * and finally the HAL does the platform's own memory setup.
 */
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
/** Worker-context calls refused by TIKU_MEM_KERNEL_ONLY since boot. */
static uint32_t mem_guard_violations;

void tiku_mem_guard_note_violation(void)
{
    mem_guard_violations++;
}

/**
 * @brief Number of worker-context allocator calls refused since boot.
 *
 * @return The running count of TIKU_MEM_KERNEL_ONLY guard violations.
 */
uint32_t tiku_mem_guard_violations(void)
{
    return mem_guard_violations;
}
#endif /* TIKU_THREADS_ENABLE */

void tiku_mem_init(void)
{
    tiku_mem_arch_size_t count;
    const tiku_mem_region_t *table;

    /* Region registry must be available before any other subsystem */
    table = tiku_region_arch_get_table(&count);
    tiku_region_init(table, count);

    /* arch_init runs FIRST so any port that mirrors .uninit out to
     * non-volatile storage (e.g. RP2350's flash backup sector) can
     * restore the SRAM working copy via plain memcpy.  Activating the
     * MPU first would write-protect .uninit before that restore could
     * land its bytes -- the MemManage handler would then reset the
     * chip on the very first persist read. */
    tiku_mem_arch_init();

    /* Activate NVM write-protection now that the working copy is in
     * place.  Subsequent persist / lc-persist / init writes go through
     * tiku_mpu_unlock_nvm() / lock_nvm() to bracket their changes. */
    tiku_mpu_init();
}
