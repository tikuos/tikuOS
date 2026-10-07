/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem.h - memory management for parts with small SRAM.
 *
 * Declares the arena (bump pointer) and pool (fixed blocks) allocators, the
 * tier allocator, the region registry, the persistent store and cells, the
 * MPU wrappers, the write-back cache, process memory and hibernate.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MEM_H_
#define TIKU_MEM_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>
#include "hal/tiku_mem_hal.h"
#include "hal/tiku_mpu_hal.h"
#include "hal/tiku_region_hal.h"

/*---------------------------------------------------------------------------*/
/* HIFRAM PLACEMENT MACROS                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @defgroup TIKU_HIFRAM HIFRAM placement attributes
 * @brief Place data-only allocations in upper FRAM on parts with a separate
 *        upper bank (FR5994, FR6989).
 *
 * Tags a variable for the matching `.upper.*` linker section; the macros expand
 * to nothing where there is no HIFRAM.  Access requires MEMORY_MODEL=large:
 * the small model emits 16-bit moves, and the reference fails to link.
 * @{
 */

/**
 * @brief A section attribute spelled for the object format.
 *
 * ELF takes the bare name.  Mach-O, met only by the host kernel tests on a
 * Mac, takes "segment,section"; a host section is never durable, so every
 * name maps to one private __DATA section there.
 *
 * @param name  ELF section name, e.g. ".persistent"
 * @note The grade, HIFRAM and cell-gate macros in this header spell their
 *       sections through this.
 */
#if defined(__APPLE__) && defined(__MACH__)
/* A Mach-O section name takes no '.' and at most sixteen characters. */
#define TIKU_SECTION(name)  __attribute__((section("__DATA,__tiku_grade")))
#else
#define TIKU_SECTION(name)  __attribute__((section(name)))
#endif

#if defined(TIKU_DEVICE_HAS_HIFRAM) && TIKU_DEVICE_HAS_HIFRAM
#define TIKU_HIFRAM      TIKU_SECTION(".upper.data")    /**< initialised data */
#define TIKU_HIFRAM_RO   TIKU_SECTION(".upper.rodata")  /**< read-only data */
#define TIKU_HIFRAM_BSS  TIKU_SECTION(".upper.bss")     /**< zeroed data */
#else
#define TIKU_HIFRAM
#define TIKU_HIFRAM_RO
#define TIKU_HIFRAM_BSS
#endif

/** @} */ /* End of TIKU_HIFRAM group */

/*---------------------------------------------------------------------------*/
/* DURABILITY GRADES                                                         */
/*---------------------------------------------------------------------------*/

/*
 * Code outside kernel/memory/ places data in these sections only through
 * these macros, never with a raw section attribute:
 *
 *   TIKU_DURABLE        survives a power cycle
 *   TIKU_DURABLE_FIRST  the layout record, at one place in every image
 *   TIKU_RETAINED       survives a warm reset; a power cycle may lose it
 *   TIKU_FRAM_SPILL     MSP430 capacity spill; promises no durability
 */

/**
 * @brief Placement for warm-reset state, not promised across a power cycle.
 *
 * Every port but MSP430 maps it to `.retained`: SRAM that the reset handler
 * does not zero and the MPU does not write-protect, so a store needs no NVM
 * window.  A new port gets this mapping by default.
 *
 * @note On MSP430 it is `.persistent` FRAM, which the MPU write-protects, so
 *       a store there needs the tiku_mpu_unlock_nvm() window.
 * @note STM32N6 and ESP32-C61 place it inside the mirrored durable image, so
 *       it also reaches NVM at each flush.
 * @note On nRF54L durable data sits in RRAM behind the RRAMC WEN gate, where
 *       a store outside the window takes a bus fault, so retained data must
 *       not map to `.persistent` there.
 */
#if defined(PLATFORM_MSP430)
#define TIKU_RETAINED  TIKU_SECTION(".persistent")
#else
#define TIKU_RETAINED  TIKU_SECTION(".retained")
#endif

/**
 * @brief Placement for state that survives a power cycle (`.persistent`).
 *
 * Outside kernel/memory/ this is the only way to place durable data;
 * tools/check_durable_placement.sh rejects a raw section attribute, because a
 * per-file "#ifdef MSP430" copy of one leaves the data volatile elsewhere.
 *
 * @note Stores go inside a tiku_mpu_unlock_nvm()/lock_nvm() window, or
 *       through the persist-cell API, which opens one itself.
 * @note Everything placed here must fit the smallest compiled target's
 *       budget, which the linker asserts, or be platform-gated.
 */
#define TIKU_DURABLE  TIKU_SECTION(".persistent")

/*
 * Where `.persistent` lives, its linker-asserted budget, and when a store
 * becomes durable:
 *
 *   MSP430       lower FRAM, in place               ample  at the store
 *   nRF54L       RRAM behind WEN, in place          16 KB  at the store
 *   RA8P1        MRAM persist partition, in place   16 KB  at the store
 *   RP2350       SRAM mirrored to a flash sector     4 KB  at relock
 *   Ambiq 4l/4p  SRAM mirrored to an MRAM page       8 KB  at relock
 *   Apollo510    SRAM mirrored to an MRAM page      16 KB  at relock
 *   STM32N6      SRAM mirrored to XSPI NOR          16 KB  at relock
 *   ESP32-C61    SRAM mirrored to flash             16 KB  at relock
 *   host         ordinary section, never durable
 *
 * A mirror's budget loses 16 bytes to the mirror header; Ambiq 4l/4p is held
 * to the 8 KB MPU region that write-protects it.
 */

/**
 * @brief Placement for the layout record, at one place in every image.
 *
 * Each linker script puts this section before anything TIKU_DURABLE places by
 * link order.  It holds one object, the record of who owns /data; with a
 * second object, their order would depend on link order.
 */
#define TIKU_DURABLE_FIRST  TIKU_SECTION(".persistent.layout")

/**
 * @brief MSP430 capacity spill for large working buffers; not durable.
 *
 * Buffers such as TLS records and TCP pools do not fit MSP430's SRAM, so there
 * they go to FRAM in `.persistent`.  Elsewhere this expands to nothing, which
 * keeps them in SRAM and out of the durable budget.
 *
 * @note Use it, not TIKU_DURABLE, for data not needed after a reset.  On
 *       MSP430 a store into it needs the NVM window like any `.persistent`
 *       data.
 */
#ifdef PLATFORM_MSP430
#define TIKU_FRAM_SPILL  TIKU_SECTION(".persistent")
#else
#define TIKU_FRAM_SPILL
#endif

/*---------------------------------------------------------------------------*/
/* ERROR CODES                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Memory subsystem error codes
 *
 * Shared by all allocator types in the memory module.
 */
typedef enum {
    TIKU_MEM_OK         = 0,    /**< Operation succeeded */
    TIKU_MEM_ERR_INVALID = -1,  /**< Invalid argument (NULL pointer, etc.) */
    TIKU_MEM_ERR_NOMEM  = -2,   /**< Out of memory */
    TIKU_MEM_ERR_FULL   = -3,   /**< No free metadata record or store slot */
    TIKU_MEM_ERR_NOT_FOUND = -4, /**< No such key, record, span or region */
    TIKU_MEM_ERR_IO    = -5,   /**< A durable write or its flush failed */
    TIKU_MEM_ERR_BUSY  = -6    /**< Live objects, a tracked descriptor or a
                                    reclaim job block the call */
} tiku_mem_err_t;

/*---------------------------------------------------------------------------*/
/* MEMORY TIER CLASSIFICATION                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Memory tier for placement-aware allocation.
 *
 * Names the intended tier for an allocator's backing store: the tier allocator
 * carves from the matching physical memory, and arena and pool control blocks
 * record it for introspection.
 */
typedef enum {
    TIKU_MEM_SRAM   = 0, /**< Volatile working memory; may span several banks */
    TIKU_MEM_NVM    = 1, /**< Persistent, slower writes; cold, stable data */
    TIKU_MEM_AUTO   = 2, /**< OS selects directly writable SRAM or HIFRAM, and
                              PSRAM last with TIKU_MEM_ALLOW_EXTERNAL */
    TIKU_MEM_HIFRAM = 3, /**< Upper FRAM bank (FR5994/FR6989, large model) */
    TIKU_MEM_PSRAM  = 4  /**< External PSRAM, volatile; present only while its
                              driver keeps it attached and mapped */
} tiku_mem_tier_t;

/** @brief Number of tier values, for arrays indexed by tier (AUTO unused). */
#define TIKU_MEM_TIER_COUNT  5

/*---------------------------------------------------------------------------*/
/* STATISTICS                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Memory usage statistics
 *
 * Snapshot of an allocator's current state. All sizes are in bytes.
 * The size type is provided by the memory HAL for the target platform.
 */
typedef struct {
    tiku_mem_arch_size_t total_bytes;  /**< Capacity of the backing buffer */
    tiku_mem_arch_size_t used_bytes;   /**< Currently allocated bytes */
    tiku_mem_arch_size_t peak_bytes;   /**< Lifetime high-water mark */
    tiku_mem_arch_size_t alloc_count;  /**< Number of successful allocations */
    tiku_mem_arch_size_t fail_count;   /**< Allocations refused for no room */
} tiku_mem_stats_t;

/*---------------------------------------------------------------------------*/
/* WORKER-THREAD CONFINEMENT GUARD                                           */
/*---------------------------------------------------------------------------*/

/*
 * Allocator bookkeeping takes no lock: the cooperative kernel serializes its
 * callers.  A preemptive worker thread (TIKU_THREADS_ENABLE) preempted inside
 * a mutator could hand out memory twice, corrupt a pool freelist or hold the
 * NVM window open, so workers are confined to pure computation: under
 * TIKU_THREADS_ENABLE the memory module's mutators refuse a call from worker
 * context (error or NULL return, violation counted).
 */

/**
 * @brief Return @p retval from a mutator entered in exception context.
 *
 * Active only with TIKU_MEM_RECLAIM_ENABLE; otherwise it compiles to nothing.
 */
#if defined(TIKU_MEM_RECLAIM_ENABLE) && TIKU_MEM_RECLAIM_ENABLE
#define TIKU_MEM_EXCEPTION_GUARD(retval) \
    do { if (TIKU_MEM_ARCH_IN_EXCEPTION()) return retval; } while (0)
#else
#define TIKU_MEM_EXCEPTION_GUARD(retval) do { } while (0)
#endif

#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
/** @brief Non-zero in kernel context, zero on a worker (tiku_thread.c). */
int tiku_thread_in_kernel(void);

/** @brief Count one mutator call refused by TIKU_MEM_KERNEL_ONLY. */
void     tiku_mem_guard_note_violation(void);

/**
 * @brief Lifetime count of allocator calls refused by the worker guard.
 *
 * Counts every rejection of a memory mutator entered from a worker thread.
 * Any non-zero value means a worker called a mutator, which returned an error
 * or NULL and changed nothing.
 *
 * @return Number of worker-context calls refused since boot
 */
uint32_t tiku_mem_guard_violations(void);

/**
 * @brief Return @p retval from a mutator not entered in kernel context.
 *
 * A worker-thread call is counted as a violation; an exception-context call
 * is refused through TIKU_MEM_EXCEPTION_GUARD.  Without TIKU_THREADS_ENABLE
 * only the exception check remains.  The _VOID form returns nothing.
 */
#define TIKU_MEM_KERNEL_ONLY(retval)              \
    do {                                          \
        TIKU_MEM_EXCEPTION_GUARD(retval);         \
        if (!tiku_thread_in_kernel()) {           \
            tiku_mem_guard_note_violation();      \
            return retval;                        \
        }                                         \
    } while (0)
#define TIKU_MEM_KERNEL_ONLY_VOID()               \
    do {                                          \
        TIKU_MEM_EXCEPTION_GUARD();               \
        if (!tiku_thread_in_kernel()) {           \
            tiku_mem_guard_note_violation();      \
            return;                               \
        }                                         \
    } while (0)
#else
#define TIKU_MEM_KERNEL_ONLY(retval)      TIKU_MEM_EXCEPTION_GUARD(retval)
#define TIKU_MEM_KERNEL_ONLY_VOID()       TIKU_MEM_EXCEPTION_GUARD()
#endif

/*---------------------------------------------------------------------------*/
/* MEMORY REGION REGISTRY                                                    */
/*---------------------------------------------------------------------------*/

/*
 * The region registry maps the physical memory layout at boot time.
 * Subsystems query it to verify that their buffers are in the correct
 * memory type (SRAM or NVM for arenas, NVM for persistent storage). Claims
 * track which subsystem owns each region to detect overlaps early.
 */

/** Maximum number of platform-defined memory regions */
#ifndef TIKU_REGION_MAX_REGIONS
#define TIKU_REGION_MAX_REGIONS  10
#endif

/** Maximum number of claimed (owned) memory regions */
#ifndef TIKU_REGION_MAX_CLAIMS
#define TIKU_REGION_MAX_CLAIMS  16
#endif

/**
 * @brief Memory region type classification
 *
 * Identifies the type of a memory region in the platform's address map.
 */
typedef enum {
    TIKU_MEM_REGION_SRAM,       /**< Volatile SRAM */
    TIKU_MEM_REGION_NVM,        /**< Non-volatile memory */
    TIKU_MEM_REGION_PERIPHERAL, /**< Memory-mapped peripheral registers */
    TIKU_MEM_REGION_FLASH       /**< Read-only flash (code, constants) */
} tiku_mem_region_type_t;

/**
 * @brief Memory region descriptor
 *
 * Describes one contiguous region in the platform's physical memory
 * map. The platform provides a const table of these at boot.
 */
typedef struct tiku_mem_region {
    const uint8_t          *base; /**< Start address of the region */
    tiku_mem_arch_size_t    size; /**< Size of the region in bytes */
    tiku_mem_region_type_t  type; /**< Region type (SRAM, NVM, etc.) */
} tiku_mem_region_t;

/**
 * @brief Claimed region descriptor (for overlap detection)
 *
 * Records that a subsystem has claimed ownership of a memory range; a claim
 * that overlaps it is refused.
 */
typedef struct {
    const uint8_t          *base;     /**< Start address of claimed range */
    tiku_mem_arch_size_t    size;     /**< Size of claimed range in bytes */
    uint8_t                 owner_id; /**< Subsystem identifier */
} tiku_mem_claimed_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — REGION REGISTRY                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the region registry with a platform-provided table
 *
 * Stores the platform's memory region table and validates that no two
 * regions overlap.
 *
 * @param table  Pointer to the platform's region descriptor array (const)
 * @param count  Number of entries in the table
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if table is NULL,
 *         count is 0, count exceeds TIKU_REGION_MAX_REGIONS, or any two
 *         regions overlap
 * @note tiku_mem_init() calls it first, before any other memory subsystem
 *       initializes.
 */
tiku_mem_err_t tiku_region_init(const tiku_mem_region_t *table,
                                 tiku_mem_arch_size_t count);

/**
 * @brief Check if a memory range falls within a region of the expected type.
 *
 * A linear scan of the region table; the whole range must lie inside one
 * matching region.  Arithmetic is done in uintptr_t to avoid overflow and
 * undefined behaviour.
 *
 * @param ptr            Start of the range to check
 * @param size           Size of the range in bytes
 * @param expected_type  Required region type
 * @return 1 if the range is fully contained in a matching region,
 *         0 otherwise
 */
int tiku_region_contains(const uint8_t *ptr,
                         tiku_mem_arch_size_t size,
                         tiku_mem_region_type_t expected_type);

/**
 * @brief Claim a memory range for a subsystem
 *
 * Registers that a subsystem has taken ownership of a range. The range
 * must fall within a declared region and must not overlap with any
 * existing claim.
 *
 * @param ptr       Start of the range to claim
 * @param size      Size of the range in bytes
 * @param owner_id  Identifier of the claiming subsystem
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if the range
 *         is not within a declared region or overlaps an existing claim,
 *         TIKU_MEM_ERR_FULL if the claimed table is full
 */
tiku_mem_err_t tiku_region_claim(const uint8_t *ptr,
                                  tiku_mem_arch_size_t size,
                                  uint8_t owner_id);

/**
 * @brief Release a previously claimed memory range
 *
 * Removes the claim identified by its base pointer. The slot is
 * cleared and becomes available for future claims.
 *
 * @param ptr  Base pointer of the claimed range to release
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_NOT_FOUND if no claim
 *         matches the given pointer
 */
tiku_mem_err_t tiku_region_unclaim(const uint8_t *ptr);

/**
 * @brief Look up the region type for an address.
 *
 * Scans the region table to find which region contains the given
 * address and returns its type.
 *
 * @param ptr       Address to look up
 * @param out_type  Output: region type of the containing region
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_NOT_FOUND if an argument is
 *         NULL or the address is not within any declared region
 */
tiku_mem_err_t tiku_region_get_type(const uint8_t *ptr,
                                     tiku_mem_region_type_t *out_type);

/*---------------------------------------------------------------------------*/
/* ARENA ALLOCATOR                                                           */
/*---------------------------------------------------------------------------*/

/** @brief Private reservation handle; all-zero for caller-owned backing. */
typedef struct {
    uint32_t generation;     /**< record generation when the handle was made */
    uint16_t slot_plus_one;  /**< reservation table slot + 1; 0 for none */
} tiku_mem_backing_t;

/**
 * @brief Arena (bump-pointer) allocator control block.
 *
 * Manages a contiguous buffer, each allocation advancing an offset.  There is
 * no individual free: tiku_arena_reset() discards everything at once, and
 * `peak` survives the reset as a lifetime maximum.
 */
typedef struct {
    uint8_t              *buf;       /**< Backing buffer */
    tiku_mem_arch_size_t  capacity;  /**< Buffer size in bytes */
    tiku_mem_arch_size_t  offset;    /**< Current bump-pointer position */
    tiku_mem_arch_size_t  peak;      /**< Lifetime high-water mark */
    tiku_mem_arch_size_t  count;     /**< Allocations since last reset */
    tiku_mem_arch_size_t  fail;      /**< Refused allocations (no room) */
    uint8_t               id;        /**< Caller label, 0 when none given */
    uint8_t               active;    /**< Non-zero if initialized */
    tiku_mem_tier_t       tier;      /**< Tier the backing came from */
    tiku_mem_backing_t    backing;   /**< Private; never copy a live arena */
    const uint8_t       *claim_base; /**< Region claim from create, or NULL */
} tiku_arena_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — ARENA                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize an arena over a caller-provided buffer.
 *
 * The caller provides the buffer (typically a static array) and keeps owning
 * it.  The buffer must lie in one SRAM or NVM region, and is claimed in the
 * region registry under @p id.
 *
 * @param arena    Arena control block to initialize
 * @param buf      Pointer to the backing buffer
 * @param size     Size of the backing buffer in bytes
 * @param id       User-assigned identifier (0-255)
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for a NULL argument, a buffer
 *         outside every SRAM and NVM region or overlapping a claim, or no
 *         room left after alignment; TIKU_MEM_ERR_BUSY while @p arena holds
 *         a tracked reservation; TIKU_MEM_ERR_FULL when the claim table is
 *         full
 */
tiku_mem_err_t tiku_arena_create(tiku_arena_t *arena, uint8_t *buf,
                                 tiku_mem_arch_size_t size, uint8_t id);

/**
 * @brief Initialize an arena without region-registry validation.
 *
 * Lightweight variant that skips tiku_region_contains() and
 * tiku_region_claim(). For library code that manages arenas over
 * embedded struct members without depending on the memory subsystem.
 *
 * @param arena    Arena control block to initialize
 * @param buf      Pointer to the backing buffer
 * @param size     Size of the backing buffer in bytes
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID if arena or buf is NULL or
 *         alignment leaves no room; TIKU_MEM_ERR_BUSY while @p arena holds a
 *         tracked reservation
 */
tiku_mem_err_t tiku_arena_create_raw(tiku_arena_t *arena, uint8_t *buf,
                                      tiku_mem_arch_size_t size);

/**
 * @brief Allocate memory from an arena.
 *
 * Bump-pointer allocation; the pointer is aligned to the platform's word
 * boundary and a request that is not a multiple of it is rounded up.  There is
 * no individual free; tiku_arena_reset() reclaims everything.
 *
 * @param arena    Arena to allocate from
 * @param size     Number of bytes requested (must be > 0)
 * @return Pointer to the allocated memory, or NULL if the arena is full,
 *         the arguments are invalid or a reclaim job holds the arena's owner
 */
void *tiku_arena_alloc(tiku_arena_t *arena, tiku_mem_arch_size_t size);

/**
 * @brief Reset an arena, reclaiming all allocations.
 *
 * Sets the offset and the allocation count back to zero in O(1).  The buffer
 * is not zeroed (tiku_arena_secure_reset() zeroes it), and the peak
 * high-water mark is kept as a lifetime maximum.
 *
 * @param arena    Arena to reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID for an invalid
 *         arena, TIKU_MEM_ERR_BUSY while a reclaim job holds its owner
 */
tiku_mem_err_t tiku_arena_reset(tiku_arena_t *arena);

/**
 * @brief Release an empty arena's backing reservation or region claim.
 *
 * A caller-owned buffer is not freed and not erased; only the arena's region
 * claim is removed.
 *
 * @param arena  Arena to destroy, already reset
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for an invalid arena;
 *         TIKU_MEM_ERR_BUSY while it holds allocations, or while a reclaim
 *         fence or a fixed reservation above it blocks the release; or the
 *         region-unclaim error
 * @note Stop every user of the arena before resetting and destroying it.
 */
tiku_mem_err_t tiku_arena_destroy(tiku_arena_t *arena);

/**
 * @brief Securely reset an arena, zeroing all memory before reclaiming.
 *
 * As tiku_arena_reset() but overwrites the buffer first, for arenas that held
 * keys or credentials.  The arch layer zeroes with volatile stores the
 * compiler cannot elide, so the reset costs O(n) in the arena's capacity.
 *
 * @param arena    Arena to securely reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID for an invalid
 *         arena, TIKU_MEM_ERR_BUSY while a reclaim job holds its owner
 */
tiku_mem_err_t tiku_arena_secure_reset(tiku_arena_t *arena);

/**
 * @brief Get current statistics for an arena.
 *
 * Fills a tiku_mem_stats_t with a snapshot of the arena's state.
 *
 * @param arena    Arena to query
 * @param stats    Output structure (caller-provided)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID for an invalid
 *         arena or a NULL @p stats
 */
tiku_mem_err_t tiku_arena_stats(const tiku_arena_t *arena,
                                tiku_mem_stats_t *stats);

/*---------------------------------------------------------------------------*/
/* POOL ALLOCATOR                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Debug poisoning for the pool allocator.
 *
 * When non-zero, a freed block is filled with 0xDE after its freelist pointer,
 * which makes use-after-free visible in a dump and usually turns silent
 * corruption into a crash.  Disable in production to avoid the per-free cost.
 */
#ifndef TIKU_POOL_DEBUG
#define TIKU_POOL_DEBUG  0
#endif

/**
 * @brief Fixed-size block pool allocator control block.
 *
 * Divides a buffer into equal blocks whose free entries chain through an
 * embedded freelist, so there is no per-block metadata: an allocated block is
 * entirely the caller's, and a freed one lends its first word.
 */
typedef struct {
    uint8_t              *buf;         /**< Backing buffer */
    tiku_mem_arch_size_t  block_size;  /**< Aligned block size in bytes */
    tiku_mem_arch_size_t  block_count; /**< Total number of blocks */
    void                 *free_head;   /**< Head of embedded freelist */
    tiku_mem_arch_size_t  used_count;  /**< Currently allocated blocks */
    tiku_mem_arch_size_t  peak_count;  /**< Lifetime high-water mark */
    uint8_t               id;          /**< Caller label, 0 when none given */
    uint8_t               active;      /**< Non-zero if initialized */
    uint8_t               nvm;         /**< Non-zero: NVM-tier backing, whose
                                            freelist writes go through
                                            tiku_tier_nvm_write() */
    tiku_mem_tier_t       tier;        /**< Tier the backing came from */
    tiku_mem_arch_size_t  fail;        /**< Refused allocations (exhausted) */
    tiku_mem_backing_t    backing;     /**< Private; never copy a live pool */
    uint8_t               reset_failed; /**< Freelist broken; reset again */
} tiku_pool_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — POOL                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a pool over a caller-provided buffer.
 *
 * Divides the buffer into blocks and chains them into the freelist.  block_size
 * is rounded up to the platform alignment and clamped to at least a pointer,
 * since a free block must hold one.  The caller owns the buffer.
 *
 * @param pool         Pool control block to initialize
 * @param buf          Backing buffer, aligned to TIKU_MEM_ARCH_ALIGNMENT and
 *                     to a pointer
 * @param block_size   Requested size of each block in bytes
 * @param block_count  Number of blocks
 * @param id           User-assigned identifier (0-255)
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID if pool or buf is NULL, buf is
 *         misaligned, or a size is 0; TIKU_MEM_ERR_BUSY while @p pool holds
 *         a tracked reservation; TIKU_MEM_ERR_NOMEM when the blocks overflow
 *         the address space
 */
tiku_mem_err_t tiku_pool_create(tiku_pool_t *pool, uint8_t *buf,
                                 tiku_mem_arch_size_t block_size,
                                 tiku_mem_arch_size_t block_count,
                                 uint8_t id);

/**
 * @brief Initialize a pool over an NVM-tier buffer.
 *
 * As tiku_pool_create(), but the pool is marked NVM-backed: the freelist is
 * built and maintained through tiku_tier_nvm_write(), since a direct store
 * faults on program-op NVM.  Blocks are read by plain pointer.
 *
 * @param pool         Pool control block to initialize
 * @param buf          NVM-tier backing buffer
 * @param block_size   Requested size of each block in bytes
 * @param block_count  Number of blocks
 * @param id           User-assigned identifier
 * @return As tiku_pool_create(), or the tiku_tier_nvm_write() error from
 *         building the freelist
 */
tiku_mem_err_t tiku_pool_create_nvm(tiku_pool_t *pool, uint8_t *buf,
                                     tiku_mem_arch_size_t block_size,
                                     tiku_mem_arch_size_t block_count,
                                     uint8_t id);

/**
 * @brief Initialize a pool with id 0.
 *
 * Same as tiku_pool_create(); neither consults the region registry.  For
 * library code that manages pools over embedded struct members.
 *
 * @param pool         Pool control block to initialize
 * @param buf          Pointer to the backing buffer
 * @param block_size   Requested size of each block in bytes
 * @param block_count  Number of blocks
 * @return As tiku_pool_create()
 */
tiku_mem_err_t tiku_pool_create_raw(tiku_pool_t *pool, uint8_t *buf,
                                     tiku_mem_arch_size_t block_size,
                                     tiku_mem_arch_size_t block_count);

/**
 * @brief Allocate a block from the pool.
 *
 * Pops the head of the embedded freelist in O(1) and tracks used_count and
 * peak_count.
 *
 * @param pool   Pool to allocate from (must be active)
 * @return Pointer to the allocated block, or NULL if the pool is empty or
 *         invalid, its last reset failed, or a reclaim job holds its owner
 */
void *tiku_pool_alloc(tiku_pool_t *pool);

/**
 * @brief Return a block to the pool.
 *
 * Pushes it back onto the freelist head in O(1), first checking that the
 * pointer is a block of this buffer that is not already free, which catches
 * another allocator's pointer, a wrong offset or a double free.
 *
 * @param pool  Pool the block came from
 * @param ptr   Block to return
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for a rejected pointer or an
 *         invalid pool; TIKU_MEM_ERR_BUSY while a reclaim job holds the
 *         pool's owner; or the tiku_tier_nvm_write() error of an NVM pool
 */
tiku_mem_err_t tiku_pool_free(tiku_pool_t *pool, void *ptr);

/**
 * @brief Get current statistics for a pool.
 *
 * Fills the caller's snapshot: total and used bytes come from the block size
 * times the block and used counts, and alloc_count is the used count.
 *
 * @param pool    Pool to query
 * @param stats   Output structure (caller-provided)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID for an invalid
 *         pool or a NULL @p stats
 */
tiku_mem_err_t tiku_pool_stats(const tiku_pool_t *pool,
                                tiku_mem_stats_t *stats);

/**
 * @brief Reset the pool, returning all blocks to the freelist.
 *
 * Re-chains all blocks into the freelist and resets used_count to zero; the
 * peak stays a lifetime figure.  O(n) in block_count.  After a failed
 * rebuild, alloc and free refuse the pool until a reset succeeds.
 *
 * @param pool   Pool to reset
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for an invalid pool;
 *         TIKU_MEM_ERR_BUSY while a reclaim job holds the pool's owner; or
 *         the tiku_tier_nvm_write() error of an NVM pool
 */
tiku_mem_err_t tiku_pool_reset(tiku_pool_t *pool);

/**
 * @brief Release a pool with no allocated blocks.
 *
 * Freeing or resetting a pool keeps its backing; this releases a tracked
 * reservation.  A caller-owned buffer is not freed.
 *
 * @param pool  Pool to destroy
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for an invalid pool;
 *         TIKU_MEM_ERR_BUSY while blocks are allocated, or while a reclaim
 *         fence or a fixed reservation above it blocks the release
 */
tiku_mem_err_t tiku_pool_destroy(tiku_pool_t *pool);

/*---------------------------------------------------------------------------*/
/* PERSISTENT NVM KEY-VALUE STORE                                            */
/*---------------------------------------------------------------------------*/

/*
 * Manages non-volatile storage with validation. Entries are registered
 * at boot with caller-provided NVM buffers. A magic number (TIKU_PERSIST_MAGIC)
 * distinguishes valid entries from uninitialized NVM, allowing the store to
 * recover registered data across reboots.
 */

/** Maximum number of persistent entries the store can hold */
#ifndef TIKU_PERSIST_MAX_ENTRIES
#define TIKU_PERSIST_MAX_ENTRIES  16
#endif

/** Maximum key length in bytes (including null terminator) */
#ifndef TIKU_PERSIST_MAX_KEY_LEN
#define TIKU_PERSIST_MAX_KEY_LEN  8
#endif

/** Magic number written into valid entries to distinguish from NVM garbage */
#define TIKU_PERSIST_MAGIC  0x544B5553U

/** Default NVM write-endurance warning threshold (cycles) */
#ifndef TIKU_PERSIST_WEAR_THRESHOLD
#define TIKU_PERSIST_WEAR_THRESHOLD  1000000000UL
#endif

/**
 * @brief One entry in the persistent store.
 *
 * Each entry maps a short string key to a caller-provided NVM buffer.
 * The magic number and valid flag together indicate whether the entry
 * contains real data or is uninitialized NVM.
 */
typedef struct {
    char      key[TIKU_PERSIST_MAX_KEY_LEN]; /**< Null-terminated key string */
    uint8_t  *fram_ptr;   /**< Pointer to caller-provided NVM buffer */
    tiku_mem_arch_size_t value_len;  /**< Current length of stored value */
    tiku_mem_arch_size_t capacity;   /**< Maximum capacity of NVM buffer */
    uint32_t  write_count; /**< Number of writes (wear monitoring) */
    uint32_t  magic;       /**< Must equal TIKU_PERSIST_MAGIC if valid */
    uint8_t   valid;       /**< Non-zero if entry is in use */
} tiku_persist_entry_t;

/**
 * @brief Persistent store control block.
 *
 * Contains an array of entries and a count of active entries.
 */
typedef struct {
    tiku_persist_entry_t entries[TIKU_PERSIST_MAX_ENTRIES];
    tiku_mem_arch_size_t count;  /**< Number of valid entries */
} tiku_persist_store_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — PERSISTENT STORE                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the persistent store, recovering valid entries.
 *
 * Scans all slots: entries with correct magic and valid flag are kept,
 * all others are cleared.
 *
 * @param store   Store to initialize
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if store is NULL,
 *         TIKU_MEM_ERR_IO when the flush at relock fails
 * @note Call once at boot.
 */
tiku_mem_err_t tiku_persist_init(tiku_persist_store_t *store);

/**
 * @brief Register an NVM buffer under a key.
 *
 * An existing key takes the new pointer and capacity and keeps its write
 * count, and keeps its length unless the capacity is smaller, which drops the
 * value; no bytes are copied.  A new key takes the first empty slot.
 *
 * @param store     Store to register into
 * @param key       Null-terminated key string
 * @param fram_buf  Caller-provided buffer inside an NVM region
 * @param capacity  Size of the NVM buffer in bytes
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID (bad argument, key too long or
 *         buffer outside NVM), TIKU_MEM_ERR_FULL, or TIKU_MEM_ERR_IO
 */
tiku_mem_err_t tiku_persist_register(tiku_persist_store_t *store,
                                     const char *key,
                                     uint8_t *fram_buf,
                                     tiku_mem_arch_size_t capacity);

/**
 * @brief Read a value from the persistent store into an SRAM buffer.
 *
 * Copies from NVM to the caller's buffer via the HAL. NVM may have
 * wait states on some platforms, so reading into SRAM gives faster
 * subsequent access.
 *
 * @param store     Store to read from
 * @param key       Key to look up
 * @param buf       Destination SRAM buffer
 * @param buf_size  Size of destination buffer
 * @param out_len   Output: actual length of stored value
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_NOT_FOUND, TIKU_MEM_ERR_NOMEM,
 *         or TIKU_MEM_ERR_INVALID
 */
tiku_mem_err_t tiku_persist_read(tiku_persist_store_t *store,
                                  const char *key,
                                  uint8_t *buf,
                                  tiku_mem_arch_size_t buf_size,
                                  tiku_mem_arch_size_t *out_len);

/**
 * @brief Write a value from SRAM into the persistent NVM store.
 *
 * Copies through the HAL, updates the length and bumps the write count for
 * wear monitoring.  It opens its own unlock window and nests, so one write
 * needs no bracket while several can still share one.
 *
 * @param store     Store to write into
 * @param key       Key to look up
 * @param data      Source data in SRAM
 * @param data_len  Length of source data
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_NOT_FOUND, TIKU_MEM_ERR_NOMEM,
 *         TIKU_MEM_ERR_INVALID, or TIKU_MEM_ERR_IO when the flush at relock
 *         fails
 */
tiku_mem_err_t tiku_persist_write(tiku_persist_store_t *store,
                                   const char *key,
                                   const uint8_t *data,
                                   tiku_mem_arch_size_t data_len);

/**
 * @brief Delete an entry from the persistent store.
 *
 * Zeroes the entry slot, so a later lookup of the key returns
 * TIKU_MEM_ERR_NOT_FOUND.
 *
 * @param store   Store to delete from
 * @param key     Key to delete
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_NOT_FOUND, TIKU_MEM_ERR_INVALID, or
 *         TIKU_MEM_ERR_IO when the flush at relock fails
 */
tiku_mem_err_t tiku_persist_delete(tiku_persist_store_t *store,
                                    const char *key);

/**
 * @brief Check wear level for a key.
 *
 * Returns the write count and whether it has reached the warning threshold;
 * NVM technologies have finite write endurance.
 *
 * @param store       Store to query
 * @param key         Key to check
 * @param write_count Output: number of writes to this key (may be NULL)
 * @return 1 once write_count reaches TIKU_PERSIST_WEAR_THRESHOLD, 0 below
 *         it, or a negative tiku_mem_err_t on error
 */
int tiku_persist_wear_check(tiku_persist_store_t *store,
                             const char *key,
                             uint32_t *write_count);

/*---------------------------------------------------------------------------*/
/* PERSISTENT CELLS                                                          */
/*---------------------------------------------------------------------------*/

/*
 * The static counterpart to the key-value store above.  A cell is a
 * single value in the `.persistent` section paired with a magic-word
 * gate, declared at compile time and validated once at boot.  Cells hold
 * boot_count, cold_boots, the device name and the RTC epoch, and each
 * follows three rules:
 *
 *   1. magic gate    — a sentinel beside the data distinguishes real
 *                      persisted state from virgin/corrupted NVM
 *   2. MPU window    — every store is bracketed by
 *                      tiku_mpu_unlock_nvm()/tiku_mpu_lock_nvm()
 *   3. commit order  — data is written before the gate, so a power
 *                      cut mid-prime leaves "virgin", never
 *                      "valid-looking garbage"
 *
 * Atomicity: the unit of power-cut atomicity is the architecture
 * word (16 bits on MSP430, 32 bits on 32-bit ports).  A wider update
 * (uint32_t on MSP430, blobs anywhere) can tear between word stores, so
 * cell_write() and cell_commit() clear the gate first and stamp it after the
 * value: a cut leaves an invalid gate and the cell re-primes.  A single-word
 * cell_write() leaves the gate alone; cell_commit() stamps it either way.
 *
 * Reads are plain variable reads: cells are memory-mapped, so a read
 * never opens the MPU window.
 *
 * Unlike the key-value store, cells have no runtime registry and no
 * string keys: the descriptor is `static const` (zero SRAM) and resolves
 * at link time.  Boot matches cells by key only to carry them across a
 * layout change.
 */

/** @brief Descriptor for one magic-gated persistent cell. */
typedef struct {
    void        *data;     /**< NVM-resident value storage            */
    uint32_t    *gate;     /**< NVM-resident magic-word gate          */
    const void  *def;      /**< default primed on first boot
                                (NULL = zero-fill)                    */
    uint16_t     size;     /**< value size in bytes                   */
    uint16_t     def_size; /**< bytes of @ref def to copy (<= size)   */
    uint32_t     key;      /**< gate value that marks the cell valid  */
} tiku_persist_cell_t;

/**
 * @brief Declare the gate and descriptor for a `.persistent` variable.
 *
 * The caller declares the value variable itself so it stays readable by name;
 * this adds the gate cell and a link-time-resolved descriptor beside it.
 *
 * @param cell     Descriptor identifier to define
 * @param var      The `.persistent` value variable (size taken by
 *                 sizeof)
 * @param key_val  Magic value gating this cell, unique per cell;
 *                 bump it when the cell's meaning/layout changes
 * @param def_ptr  Default value primed on first boot (NULL = zeros)
 * @param def_len  Bytes of @p def_ptr to copy (0 with NULL)
 */
#define TIKU_PERSIST_CELL(cell, var, key_val, def_ptr, def_len)        \
    static uint32_t TIKU_SECTION(".persistent")                        \
        cell##_gate;                                                   \
    static const tiku_persist_cell_t cell = {                          \
        &(var), &cell##_gate, (def_ptr),                               \
        (uint16_t)sizeof(var), (def_len), (key_val)                    \
    };                                                                 \
    static const tiku_persist_cell_t *const cell##_entry               \
        TIKU_CELL_TABLE_ATTR = &(cell)

/**
 * @brief Places a cell's table entry where boot can find it by key.
 *
 * With TIKU_CELL_TABLE (every port but MSP430; the Makefile defines it) the
 * linker collects `.tiku_cells` between __tiku_cells_start and
 * __tiku_cells_end, so boot can carry each cell after an update moves it.
 *
 * @note Without the table the entry is an unused constant.
 */
#if defined(TIKU_CELL_TABLE) && TIKU_CELL_TABLE
#define TIKU_CELL_TABLE_ATTR  __attribute__((section(".tiku_cells"), used))
#else
#define TIKU_CELL_TABLE_ATTR  __attribute__((unused))
#endif

/**
 * @brief Validate a cell's gate; prime defaults on a virgin NVM.
 *
 * A gate already holding the key means the value is real and untouched;
 * otherwise the cell is zero-filled, the default copied in, and the gate
 * stamped last.
 *
 * @param c  Cell descriptor
 * @return 1 when the cell was primed this boot (virgin or corrupted
 *         NVM, or a cell the layout move could not carry), 0 when the
 *         persisted value was kept
 * @note Call once at boot per cell, before the first read.
 */
uint8_t tiku_persist_cell_init(const tiku_persist_cell_t *c);

/**
 * @brief Report whether a cell's gate currently validates.
 *
 * Lock-free single compare.  Read paths that may run before cell_init(),
 * or that check for in-field corruption, test it (see
 * tiku_rtc_get_seconds()).
 *
 * @param c  Cell descriptor
 * @return Non-zero when the gate holds the key
 */
uint8_t tiku_persist_cell_valid(const tiku_persist_cell_t *c);

/**
 * @brief Unchecked cell update; use the status variant to verify completion.
 *
 * Copies min(@p len, cell size) bytes under one MPU unlock window.  For a
 * write that also validates a gate that was not valid, use
 * tiku_persist_cell_commit().
 *
 * @param c    Cell descriptor
 * @param src  New value bytes
 * @param len  Bytes to copy (clamped to the cell size)
 * @note Call after cell_init() has validated the gate.
 */
void tiku_persist_cell_write(const tiku_persist_cell_t *c,
                             const void *src, uint16_t len);

/**
 * @brief Unchecked cell commit; data precedes the gate store.
 *
 * Wide writes invalidate the gate before copying. Flush errors are discarded;
 * neither the previous value nor the saved mirror is guaranteed on failure.
 *
 * @param c    Cell descriptor
 * @param src  New value bytes
 * @param len  Bytes to copy (clamped to the cell size)
 */
void tiku_persist_cell_commit(const tiku_persist_cell_t *c,
                              const void *src, uint16_t len);

/**
 * @brief Unchecked word write for uint32_t cells.
 *
 * Equivalent to tiku_persist_cell_write(c, &v, 4).  Single store on
 * 32-bit targets; on MSP430 two 16-bit stores, with the gate cleared
 * before them and stamped after.
 *
 * @param c  Cell descriptor (size 4; another size takes the first
 *           min(size, 4) bytes of @p v)
 * @param v  New value
 */
void tiku_persist_cell_write_u32(const tiku_persist_cell_t *c,
                                 uint32_t v);

/**
 * @brief tiku_persist_cell_write() that reports completion.
 *
 * The void entry points discard this status.  An error may leave the
 * working copy and gate changed and the medium uncertain; nothing is rolled
 * back or retried.
 *
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID on a NULL argument, or
 *         TIKU_MEM_ERR_IO when the flush at relock fails
 */
tiku_mem_err_t tiku_persist_cell_write_status(const tiku_persist_cell_t *c,
                                              const void *src, uint16_t len);

/** @brief tiku_persist_cell_commit() that returns its completion status. */
tiku_mem_err_t tiku_persist_cell_commit_status(const tiku_persist_cell_t *c,
                                               const void *src, uint16_t len);

/** @brief tiku_persist_cell_write_u32() that returns its completion status. */
tiku_mem_err_t tiku_persist_cell_write_u32_status(const tiku_persist_cell_t *c,
                                                  uint32_t v);

/**
 * @brief Number of cells validated by cell_init() this boot.
 *
 * Exposed at /sys/persist/cells.
 *
 * @return Count of cell_init() calls since reset
 */
uint8_t tiku_persist_cell_count(void);

/**
 * @brief Number of cells that had to be primed this boot.
 *
 * Exposed at /sys/persist/primed and normally 0 on an established device.
 * Non-zero means cells the image adds or could not carry across a layout
 * change, a wiped store, or corruption.
 *
 * @return Count of cell_init() calls that returned 1 since reset
 */
uint8_t tiku_persist_cell_primed(void);

/*
 * Cells across a layout change.  A cell sits where link order put it, so an
 * update that adds or drops a durable variable linked before it moves it.
 * Each image records where it keeps every cell in a manifest inside its own
 * durable image.  With the cell table, the first boot of an image whose layout
 * differs finds the manifest the last image wrote in the durable image as
 * last persisted, and moves each cell whose key and size still match, value
 * first, gate last; every other cell's gate is cleared, so it re-primes.  A
 * missing or damaged manifest resets every unverified cell.  An interrupted
 * move may lose values but never validates one by a leftover gate alone.
 */

/** @brief Where one cell lives, as offsets into the durable image. */
typedef struct {
    uint32_t key;    /**< the cell's gate key */
    uint16_t gate;   /**< offset of the gate word */
    uint16_t data;   /**< offset of the value */
    uint16_t size;   /**< value size in bytes */
    uint16_t rsvd;   /**< reserved */
} tiku_persist_where_t;

/** @brief Cells one manifest can record. */
#define TIKU_PERSIST_MANIFEST_MAX    24u
/** @brief Magic of a valid manifest ("CELM"). */
#define TIKU_PERSIST_MANIFEST_MAGIC  0x4D4C4543UL   /* "CELM" */

/** @brief Every cell of one image, as that image laid them out. */
typedef struct {
    uint32_t             magic;   /**< TIKU_PERSIST_MANIFEST_MAGIC       */
    uint16_t             count;   /**< entries in use                    */
    uint16_t             rsvd;    /**< reserved                          */
    uint32_t             check;   /**< FNV-1a over count and the entries */
    tiku_persist_where_t at[TIKU_PERSIST_MANIFEST_MAX]; /**< the entries */
} tiku_persist_manifest_t;

/** @brief What a move works on; boot fills it from the linker and the HAL. */
typedef struct {
    const tiku_persist_cell_t *const *cells;  /**< this image's cells     */
    size_t                     n_cells;       /**< entries in @c cells    */
    uint8_t                   *live;          /**< where they live now    */
    size_t                     live_len;      /**< bytes at @c live       */
    const uint8_t             *old;     /**< durable image as last persisted */
    size_t                     old_len;       /**< bytes at @c old        */
    tiku_persist_manifest_t   *manifest;      /**< this image's, in live  */
    /** Optional restore of a control record, run inside the write window
     *  after the cells are snapshotted and old manifests retired; it must
     *  not flush or relock. */
    void (*preserve)(void *ctx);
    void *preserve_ctx;                       /**< argument to preserve   */
} tiku_persist_move_env_t;

/**
 * @brief Move each cell to where this image keeps it, then record the layout.
 *
 * @return Cells moved (0 when the layout is the recorded one), or -1 for a
 *         bad environment or when the write window did not complete.
 */
int tiku_persist_move(const tiku_persist_move_env_t *e);

/**
 * @brief tiku_persist_move() that also carries the layout record.
 *
 * While the record of who owns /data is not pinned at its fixed place, the
 * move captures it from the old durable image and writes it there.
 *
 * @return As tiku_persist_move()
 * @note Defined only with TIKU_CELL_TABLE.
 */
int tiku_persist_move_boot_env(tiku_persist_move_env_t *e);

/**
 * @brief tiku_persist_move_boot_env() for this image.
 * @note tiku_mem_init() calls it; defined only with TIKU_CELL_TABLE.
 */
void tiku_persist_move_boot(void);

/** @brief What this boot's move returned (0 where there is no cell table). */
int tiku_persist_moved(void);

/** @brief The layout this image records for its cells, or NULL for none. */
const tiku_persist_manifest_t *tiku_persist_manifest(void);

/*---------------------------------------------------------------------------*/
/* MPU (MEMORY PROTECTION UNIT)                                              */
/*---------------------------------------------------------------------------*/

/*
 * NVM write-protection via the platform MPU (HAL layer).
 *
 * Default policy, where the port enforces one: durable memory is
 * read+execute, no write, which keeps stray pointers and runaway code from
 * corrupting it.
 *
 * To perform an intentional NVM write the caller must explicitly
 * unlock, write, and relock:
 *
 *   uint16_t saved = tiku_mpu_unlock_nvm();
 *   (store into NVM)
 *   tiku_mpu_lock_nvm(saved);
 *
 * tiku_mpu_scoped_write() wraps the full sequence with interrupts disabled,
 * so no ISR can write NVM while it is unlocked.
 */

/**
 * @brief MPU segment identifiers.
 *
 * The HAL presents protection as the three segments of the MSP430 MPU; other
 * ports emulate that model.
 */
typedef enum {
    TIKU_MPU_SEG1 = 0,
    TIKU_MPU_SEG2 = 1,
    TIKU_MPU_SEG3 = 2
} tiku_mpu_seg_t;

/**
 * @brief MPU permission flags.
 *
 * Bit-field values that the HAL maps to the platform's MPU register
 * layout. Combine with bitwise OR for compound permissions.
 */
typedef enum {
    TIKU_MPU_READ  = 0x01,
    TIKU_MPU_WRITE = 0x02,
    TIKU_MPU_EXEC  = 0x04,
    TIKU_MPU_RD_WR   = 0x03,
    TIKU_MPU_RD_EXEC = 0x05,
    TIKU_MPU_ALL     = 0x07
} tiku_mpu_perm_t;

/** Function pointer type for tiku_mpu_scoped_write callback */
typedef void (*tiku_mpu_write_fn)(void *ctx);

/**
 * @brief Initialize the MPU with default NVM protection.
 *
 * Runs the port's protection setup, then the default protection; where the
 * port enforces it, durable memory is writable only inside an unlock window.
 * TIKU_MPU_NMI_ON_VIOLATION builds also arm the violation NMI.
 *
 * @note tiku_mem_init() calls it after the platform memory setup, so a
 *       mirrored durable image is restored before protection starts.
 */
void tiku_mpu_init(void);

/**
 * @brief Make the loadable-module execution window executable, or writable.
 *
 * W^X in time: the window holds one permission at a time, RW+XN while a
 * module is loaded and RO+X while it runs.  A no-op where the module runs
 * XIP.
 *
 * @param enable  1 = executable/read-only, 0 = writable/execute-never.
 */
void tiku_mpu_module_window_exec(int enable);

/**
 * @brief Set permissions on a single MPU segment.
 *
 * @param seg    Segment to configure (0-2)
 * @param perm   Permission flags (combination of TIKU_MPU_* values)
 */
void tiku_mpu_set_permissions(tiku_mpu_seg_t seg, tiku_mpu_perm_t perm);

/**
 * @brief Unlock NVM for writing on all segments.
 *
 * Adds write permission to all segments via the arch layer. Returns
 * an opaque saved state so it can be restored by tiku_mpu_lock_nvm().
 *
 * @return Previous MPU state value
 */
uint16_t tiku_mpu_unlock_nvm(void);

/**
 * @brief Flush durable writes and restore the saved MPU state.
 *
 * On a mirror port the flush is what commits the window's writes; this form
 * discards its result.
 *
 * @param saved_state  Value returned by a prior tiku_mpu_unlock_nvm()
 */
void tiku_mpu_lock_nvm(uint16_t saved_state);

/**
 * @brief tiku_mpu_lock_nvm() that reports the flush; protection is restored
 *        either way.
 *
 * @param saved_state  Value returned by a prior tiku_mpu_unlock_nvm()
 * @return TIKU_MEM_OK, or TIKU_MEM_ERR_IO when the flush fails
 */
tiku_mem_err_t tiku_mpu_lock_nvm_status(uint16_t saved_state);

/**
 * @brief Execute a function with NVM unlocked, interrupts disabled.
 *
 * Disables interrupts, unlocks NVM, calls fn(ctx), relocks NVM,
 * and restores the interrupt state present at entry.
 *
 * @param fn   Function to call while NVM is writable
 * @param ctx  Opaque context pointer passed to fn
 * @note Keep @p fn short: interrupts stay masked while it runs.  Interrupts
 *       return to the state present at entry.
 */
void tiku_mpu_scoped_write(tiku_mpu_write_fn fn, void *ctx);

/**
 * @brief Raise an interrupt on an MPU violation where the platform can.
 *
 * On MSP430 the default drops a protected write unreported; this arms the
 * violation NMI so the write is seen without a reset.  Other ports already
 * fault on a violation, or enforce nothing, and are unchanged.
 *
 * @note Call before any intentional violation testing.
 */
void tiku_mpu_enable_violation_nmi(void);

/**
 * @brief Read MPU violation flags.
 *
 * Non-zero once a violation has been recorded since the last clear.  The
 * encoding is the port's: on MSP430 bit n - 1 is segment n, and
 * tiku_mpu_arch_get_violation_flags() lists the others.
 *
 * @return Violation flags; 0 on a port that records none
 */
uint16_t tiku_mpu_get_violation_flags(void);

/**
 * @brief Clear all MPU violation flags.
 *
 * MSP430 zeroes its latch and the MPUCTL1 flags and leaves the violation NMI
 * on; RA8P1 clears the fault record its flags come from; RP2350 and Ambiq
 * zero their software flag word.
 */
void tiku_mpu_clear_violation_flags(void);

/**
 * @brief Persistent count of MPU violations across warm reset.
 *
 * Monotonic since the last cold power-up on platforms that keep a diagnostic
 * section through a fault reset; 0 where there is no persistent state.
 *
 * @return Total MPU violations observed since the last cold boot.
 */
uint32_t tiku_mpu_get_violation_count(void);

/**
 * @brief Address that triggered the most recent MPU violation.
 *
 * The fault-address snapshot taken by the fault handler.  It survives a warm
 * reset, so the boot after the fault can report the address.
 *
 * @return Last faulting address, or 0 if no fault has been recorded or the
 *         port keeps no fault record.
 */
uint32_t tiku_mpu_get_last_fault_addr(void);

/*---------------------------------------------------------------------------*/
/* WORKING-MEMORY REQUESTS                                                   */
/*---------------------------------------------------------------------------*/

/** @brief How a reservation's backing is placed and released. */
typedef enum {
    TIKU_MEM_CLASS_DEFAULT = 0,  /**< RESTARTABLE with an owner, otherwise
                                      TRANSIENT */
    TIKU_MEM_TRANSIENT = 1,  /**< Creator releases backing in any order */
    TIKU_MEM_RESTARTABLE = 2,/**< Registered owner; needs reclaim enabled */
    TIKU_MEM_FIXED = 3      /**< Lower placement; release in reverse
                                 address order */
} tiku_mem_class_t;

/** @brief Handle of an owner registered through tiku_reclaim.h; 0 for none. */
typedef struct {
    uint32_t generation;     /**< owner slot generation at registration */
    uint16_t slot_plus_one;  /**< owner table slot + 1 */
} tiku_mem_owner_t;

/**
 * @brief Optional allocation settings for ordinary CPU working memory.
 *
 * NULL, or a zeroed structure, means natural alignment and automatic placement
 * in internal memory (SRAM, eligible TCM, or CPU-writable upper FRAM).
 * External memory is used only with TIKU_MEM_ALLOW_EXTERNAL, after internal.
 *
 * @note Initialize the tiers first. Arenas and pools share this type,
 *       and none of them selects protected NVM, starts an external controller,
 *       or promises zeroed, persistent or DMA-safe memory.
 */
typedef struct {
    tiku_mem_arch_size_t alignment; /**< 0: natural; otherwise power of two */
    uint16_t flags;                 /**< TIKU_MEM_ALLOW_EXTERNAL or zero */
    tiku_mem_class_t allocation_class; /**< See tiku_mem_class_t */
    tiku_mem_owner_t owner;         /**< Registered owner, or zero */
    uint16_t owner_slot;            /**< Owner's key for this reservation;
                                         non-zero with an owner, else zero */
} tiku_mem_request_t;

/** @brief Initializer for a request with every option at its default. */
#define TIKU_MEM_REQUEST_DEFAULT  { 0 }
/** @brief Request flag: external memory (PSRAM) may back the allocation. */
#define TIKU_MEM_ALLOW_EXTERNAL   0x0001u

/** @brief Reservation records shared by every tier-backed arena and pool. */
#ifndef TIKU_MEM_MAX_RESERVATIONS
#ifdef PLATFORM_MSP430
#define TIKU_MEM_MAX_RESERVATIONS 8
#else
#define TIKU_MEM_MAX_RESERVATIONS 32
#endif
#endif

/**
 * @brief Free space and reservations of one backing span.
 *
 * Free gaps include alignment gaps, even where a class's placement rules
 * would not use them.
 */
typedef struct {
    tiku_mem_arch_size_t free_bytes;   /**< bytes in no reservation */
    tiku_mem_arch_size_t largest_gap;  /**< largest contiguous free gap */
    tiku_mem_arch_size_t split_free;   /**< free bytes outside that gap */
    tiku_mem_arch_size_t live_bytes;   /**< bytes in live reservations */
    tiku_mem_arch_size_t held_bytes;   /**< bytes in reservations not live */
    uint16_t live_records;             /**< number of live reservations */
} tiku_mem_space_t;

/**
 * @brief Report the free gaps and reservations of one backing span.
 *
 * @param tier        Concrete tier (not AUTO)
 * @param span_index  Span within the tier; 0 is the primary span
 * @param space       Output
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID for a NULL @p space, or
 *         TIKU_MEM_ERR_NOT_FOUND when the span does not exist or is not
 *         initialized
 */
tiku_mem_err_t tiku_tier_span_space(tiku_mem_tier_t tier, uint8_t span_index,
                                    tiku_mem_space_t *space);

/** @brief One reservation record, as tiku_mem_reservation_next() reports it. */
typedef struct {
    tiku_mem_backing_t handle;  /**< handle of the reservation */
    tiku_mem_owner_t owner;     /**< registered owner, or zero */
    uint16_t owner_slot;        /**< owner's key, or zero */
    tiku_mem_tier_t tier;       /**< tier holding the backing */
    tiku_mem_arch_size_t offset, length, alignment; /**< within the span */
    /** Span index, kind (1 arena, 2 pool), allocation class, and state
     *  (1 live, 2 held, 3 initializing). */
    uint8_t span_index, kind, allocation_class, state;
} tiku_mem_reservation_info_t;

/**
 * @brief Report the next reservation record after @p cursor.
 *
 * Reads only the record table, never the backing memory.
 *
 * @param cursor  Set to zero before the first call; advanced past each record
 * @param info    Output
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID on a NULL argument, or
 *         TIKU_MEM_ERR_NOT_FOUND after the last record
 */
tiku_mem_err_t tiku_mem_reservation_next(uint16_t *cursor,
                                         tiku_mem_reservation_info_t *info);

/**
 * @brief Create a working arena; prefer this over named tiers in applications.
 *
 * The alignment option applies to the backing base; tiku_arena_alloc() still
 * guarantees only natural alignment for each object.  The control block's id
 * is set to zero.
 *
 * @note External backing is considered last and only with permission.  Arena
 *       reset does not return backing capacity or unblock external-memory
 *       detach.
 *
 * @param arena Output, unchanged on failure. Do not overwrite an active arena.
 * @param size Number of bytes requested; must be nonzero.
 * @param options Allocation options, or NULL for standard alignment and
 *                automatically selected internal memory.
 * @return OK; INVALID for bad arguments or options; BUSY while @p arena holds
 *         a tracked reservation, the owner's key is in use, or a reclaim job
 *         or fence blocks the request; NOMEM when no eligible span fits; FULL
 *         when no reservation record is free.
 */
tiku_mem_err_t tiku_mem_arena_create(tiku_arena_t *arena,
        tiku_mem_arch_size_t size, const tiku_mem_request_t *options);

/**
 * @brief Create a working pool with aligned fixed-size blocks.
 *
 * The block stride includes padding and free-list pointer alignment.  Freeing
 * every block does not release the backing reservation.  The control block's
 * id is set to zero.
 *
 * @param pool Output control block, unchanged on failure; do not overwrite an
 *             active pool.
 * @param block_size Requested bytes per block; must be nonzero.
 * @param block_count Number of blocks; must be nonzero.
 * @param options Allocation options, or NULL for standard alignment and
 *                automatically selected internal memory.
 * @return As tiku_mem_arena_create(), or the error from building the pool.
 */
tiku_mem_err_t tiku_mem_pool_create(tiku_pool_t *pool,
        tiku_mem_arch_size_t block_size, tiku_mem_arch_size_t block_count,
        const tiku_mem_request_t *options);

/** @brief A temporary arena, released whole by tiku_mem_workspace_close(). */
typedef tiku_arena_t tiku_mem_workspace_t;

/** @brief Open a workspace; the same as tiku_mem_arena_create(). */
tiku_mem_err_t tiku_mem_workspace_open(tiku_mem_workspace_t *workspace,
        tiku_mem_arch_size_t size, const tiku_mem_request_t *options);

/**
 * @brief Reset a workspace, then release its whole reservation.
 *
 * @return TIKU_MEM_OK, or the tiku_arena_reset() or tiku_arena_destroy()
 *         error
 * @note Stop every user of the workspace first.
 */
tiku_mem_err_t tiku_mem_workspace_close(tiku_mem_workspace_t *workspace);

/*---------------------------------------------------------------------------*/
/* TIER ALLOCATOR                                                            */
/*---------------------------------------------------------------------------*/

/*
 * The tier allocator manages backing pools for the SRAM, NVM, HIFRAM and
 * PSRAM tiers. The caller names a memory tier and the tier allocator carves
 * the buffer from that tier's backing pool; the arena or pool built over it
 * works like one over a caller-provided buffer.
 *
 * Usage, after tiku_mem_init():
 *   tiku_tier_init();
 *   tiku_tier_arena_create(&arena, TIKU_MEM_SRAM, 64, 1);
 *   void *p = tiku_arena_alloc(&arena, 16);
 *
 * NVM-backed pools: creation, reset and free initialize/update free-list links
 * through the checked NVM write path, including protection and commit status.
 * Allocation reads the next link and updates ordinary descriptor metadata.
 * These calls do not persist the pool's control structure or user payload.
 */

/**
 * @brief Size in bytes of the SRAM tier array on MSP430 and the host.
 *
 * Every other port carves the SRAM tier in its linker script
 * (TIKU_TIER_SRAM_DERIVED) and ignores this.
 */
#ifndef TIKU_TIER_SRAM_SIZE
#define TIKU_TIER_SRAM_SIZE  128
#endif

/**
 * @brief Size in bytes of MSP430's NVM tier, a lower-FRAM array.
 *
 * The array is separate from the HIFRAM tier and the pinned region backend.
 * Other ports take the NVM tier from the front of the carved NVM region, as
 * the layout service sizes it; tier statistics report the capacity.
 */
#ifndef TIKU_TIER_NVM_SIZE
#define TIKU_TIER_NVM_SIZE   1024
#endif

/**
 * @brief Size in bytes of the HIFRAM tier array.
 *
 * Compiled in only for a large-model build on a part that has HIFRAM.  It
 * must stay below 64 KB, since tiku_mem_arch_size_t is 16-bit on MSP430.
 */
#ifndef TIKU_TIER_HIFRAM_SIZE
#define TIKU_TIER_HIFRAM_SIZE  (32U * 1024U)
#endif

/**
 * @brief Size from which an AUTO allocation prefers HIFRAM.
 *
 * An allocation this size or larger goes to HIFRAM when it is available and
 * has room; smaller ones stay in SRAM.  0 disables the preference, and HIFRAM
 * remains a fallback when SRAM is full.
 */
#ifndef TIKU_TIER_AUTO_HIFRAM_THRESHOLD
#define TIKU_TIER_AUTO_HIFRAM_THRESHOLD  1024U
#endif

/**
 * @brief Initialize the tier allocator.
 *
 * Idempotent: the first call wires the backing pools and rewinds them, later
 * calls return at once so a boot-time init cannot orphan what a lazy caller
 * already allocated.  Use tiku_tier_reset() for a clean slate.
 *
 * @return TIKU_MEM_OK, or TIKU_MEM_ERR_INVALID outside kernel context
 * @note Call after tiku_mem_init().
 */
tiku_mem_err_t tiku_tier_init(void);

/**
 * @brief Attach a late-arriving backing pool as the PSRAM tier.
 *
 * External PSRAM is memory only while its driver has the device powered, timed
 * and mapped, which happens long after tier init; the driver's bring-up calls
 * this.
 *
 * @param base  Start of the mapped aperture
 * @param size  Bytes available
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for a bad range, a range that
 *         overlaps another tier, or a tier already attached;
 *         TIKU_MEM_ERR_BUSY while a reclaim fence holds the tier
 */
tiku_mem_err_t tiku_tier_attach_psram(void *base, tiku_mem_arch_size_t size);

/**
 * @brief Detach the PSRAM tier (power-down path).
 *
 * Ordinary detach returns BUSY while any backing reservation remains,
 * including an empty arena or pool, so destroy those first.  A forced detach
 * invalidates their handles without touching the backing.
 *
 * @param force  Non-zero to detach over live reservations
 * @return TIKU_MEM_OK, also when the tier is not attached, or
 *         TIKU_MEM_ERR_BUSY
 * @note Stop every user of PSRAM before a forced detach.
 */
tiku_mem_err_t tiku_tier_detach_psram(int force);

/**
 * @brief Refuse (non-zero) or allow (0) new reservations in the PSRAM tier;
 *        the PSRAM driver refuses them while the device is in half sleep.
 *
 * Live reservations are left in place; their memory must not be touched
 * until the driver allows new ones again.  Detach and reset allow them.
 *
 * @return TIKU_MEM_OK, or TIKU_MEM_ERR_INVALID outside kernel context
 */
tiku_mem_err_t tiku_tier_suspend_psram(int suspended);

/**
 * @brief Reset every tier pool to empty (destructive rewind).
 *
 * Re-wires each tier and zeroes its counters, bypassing the idempotent guard
 * in init and orphaning any sub-arena handed out; it serves teardown and test
 * isolation.  NVM backing is not zeroed.
 *
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_BUSY while a reclaim fence, job or ticket
 *         is live, or TIKU_MEM_ERR_INVALID outside kernel context
 */
tiku_mem_err_t tiku_tier_reset(void);

/**
 * @brief Create an arena backed by the specified memory tier.
 *
 * Allocates a buffer from the tier's backing pool and initializes
 * an arena over it, which works like one from tiku_arena_create().
 *
 * @param arena  Arena control block to initialize
 * @param tier   Memory tier (SRAM, NVM, HIFRAM, PSRAM, or AUTO)
 * @param size   Desired arena capacity in bytes
 * @param id     User-assigned identifier (0-255)
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID, TIKU_MEM_ERR_NOMEM,
 *         TIKU_MEM_ERR_BUSY or TIKU_MEM_ERR_FULL, as for
 *         tiku_mem_arena_create()
 */
tiku_mem_err_t tiku_tier_arena_create(tiku_arena_t *arena,
                                       tiku_mem_tier_t tier,
                                       tiku_mem_arch_size_t size,
                                       uint8_t id);

/**
 * @brief Allocate from exactly one backing span, never falling back elsewhere.
 *
 * Requires a concrete tier, not AUTO. On Apollo, SRAM span 0 is shared SRAM;
 * select it when a driver requires shared SRAM instead of CPU-local TCM.
 * Span selection alone does not guarantee DMA alignment or cache coherence.
 *
 * @param alignment  Base alignment: 0 for natural, otherwise a power of two
 * @return As tiku_tier_arena_create()
 */
tiku_mem_err_t tiku_tier_arena_create_span(tiku_arena_t *arena,
        tiku_mem_tier_t tier, uint8_t span_index, tiku_mem_arch_size_t size,
        tiku_mem_arch_size_t alignment, uint8_t id);

/**
 * @brief tiku_tier_arena_create() with class and alignment options.
 *
 * TIKU_MEM_ALLOW_EXTERNAL is invalid here: the tier already names the
 * storage.
 *
 * @return As tiku_tier_arena_create()
 */
tiku_mem_err_t tiku_tier_arena_create_opts(tiku_arena_t *arena,
        tiku_mem_tier_t tier, tiku_mem_arch_size_t size, uint8_t id,
        const tiku_mem_request_t *options);

/** @brief tiku_tier_arena_create_span() with options; no span fallback. */
tiku_mem_err_t tiku_tier_arena_create_span_opts(tiku_arena_t *arena,
        tiku_mem_tier_t tier, uint8_t span_index, tiku_mem_arch_size_t size,
        uint8_t id, const tiku_mem_request_t *options);

/** @brief tiku_tier_pool_create() with options, as for the arena form. */
tiku_mem_err_t tiku_tier_pool_create_opts(tiku_pool_t *pool,
        tiku_mem_tier_t tier, tiku_mem_arch_size_t block_size,
        tiku_mem_arch_size_t block_count, uint8_t id,
        const tiku_mem_request_t *options);

/**
 * @brief Create a pool backed by the specified memory tier.
 *
 * Allocates a buffer from the tier's backing pool and initializes
 * a fixed-size block pool over it. An NVM-tier pool builds its freelist
 * through tiku_tier_nvm_write(), which opens the NVM window itself.
 *
 * @param pool         Pool control block to initialize
 * @param tier         Memory tier (SRAM, NVM, HIFRAM, PSRAM, or AUTO)
 * @param block_size   Size of each block in bytes
 * @param block_count  Number of blocks
 * @param id           User-assigned identifier (0-255)
 * @return As tiku_tier_arena_create(), or the error from building the pool
 */
tiku_mem_err_t tiku_tier_pool_create(tiku_pool_t *pool,
                                      tiku_mem_tier_t tier,
                                      tiku_mem_arch_size_t block_size,
                                      tiku_mem_arch_size_t block_count,
                                      uint8_t id);

/**
 * @brief Query which memory tier a pointer belongs to.
 *
 * Checks the tier allocator's own backing pools first, then falls
 * back to the region registry.
 *
 * @param ptr       Address to query
 * @param out_tier  Output: memory tier of the containing region
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on a NULL argument,
 *         TIKU_MEM_ERR_NOT_FOUND if the address is not in any known memory
 *         region
 */
tiku_mem_err_t tiku_tier_get(const uint8_t *ptr,
                              tiku_mem_tier_t *out_tier);

/**
 * @brief Get usage statistics for a tier's backing pool.
 *
 * Totals cover all backing spans; all live reservations count as used.
 * Free capacity can be split: a single allocation must fit in one span.
 *
 * @param tier   Memory tier to query (SRAM, NVM, HIFRAM or PSRAM; not AUTO)
 * @param stats  Output statistics
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if stats is NULL,
 *         tier is AUTO, or the tier is not initialized
 */
tiku_mem_err_t tiku_tier_stats(tiku_mem_tier_t tier,
                                tiku_mem_stats_t *stats);

/**
 * @brief Inspect one contiguous backing span; NOT_FOUND ends enumeration.
 *
 * Tier totals aggregate spans, but each allocation must fit within one span.
 * Index zero is the primary span, which also counts failed whole-tier
 * requests.  This does not initialise or change the tier.
 *
 * @param tier   Concrete tier (not AUTO)
 * @param index  Span index
 * @param base   Output: start of the span
 * @param stats  Output: the span's statistics
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID on a NULL output, or
 *         TIKU_MEM_ERR_NOT_FOUND when the span does not exist or is not
 *         initialized
 */
tiku_mem_err_t tiku_tier_span_stats(tiku_mem_tier_t tier, uint8_t index,
                                    const uint8_t **base,
                                    tiku_mem_stats_t *stats);

/**
 * @brief Write into NVM-tier memory through its backing path.
 *
 * NVM-tier memory reads by plain pointer, but a write inside the carved NVM
 * region goes through the region backend's write operation (on Ambiq the
 * bootrom programs MRAM), while MSP430's FRAM tier array is written in place.
 *
 * @param dst  Destination inside NVM-tier memory.
 * @param src  Source bytes.
 * @param len  Byte count.
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID on a NULL argument, a range
 *         outside the region or tier, or a board with no NVM tier;
 *         TIKU_MEM_ERR_IO when the backend write or the relock flush fails.
 * @note Opens and closes the NVM window itself.
 */
tiku_mem_err_t tiku_tier_nvm_write(void *dst, const void *src,
                                   tiku_mem_arch_size_t len);

/*---------------------------------------------------------------------------*/
/* WRITE-BACK CACHE                                                          */
/*---------------------------------------------------------------------------*/

/*
 * Write-back buffer for hot NVM regions.
 *
 * NVM writes cost more energy than SRAM writes and wear the medium, so
 * frequently updated data (network stack state, sensor buffers, counters)
 * is worked on in an SRAM copy and flushed to NVM only before sleep or at
 * explicit sync points.
 *
 * Usage:
 *   static tiku_cached_region_t my_cache;
 *   static uint8_t sram_buf[sizeof(my_data_t)];
 *
 *   tiku_cache_create(&my_cache, nvm_addr, sram_buf, sizeof(my_data_t));
 *   my_data_t *p = tiku_cache_get(&my_cache);
 *   p->field = value;
 *   tiku_cache_flush(&my_cache);
 *
 * The flush opens and closes the NVM window itself.  Callers flush every
 * region before a sleep that loses SRAM.
 */

/** Maximum number of cached regions tracked by tiku_cache_flush_all() */
#ifndef TIKU_CACHE_MAX_REGIONS
#define TIKU_CACHE_MAX_REGIONS  8
#endif

/**
 * @brief Cached NVM region descriptor
 *
 * Pairs an SRAM working copy with its NVM backing.  The dirty flag marks
 * a working copy that may differ from the backing.
 */
typedef struct {
    uint8_t              *sram_cache;    /**< SRAM working copy */
    uint8_t              *fram_backing;  /**< NVM copy */
    tiku_mem_arch_size_t  size;          /**< Region size in bytes */
    uint8_t               dirty;         /**< Non-zero until the next flush */
    uint8_t               active;        /**< Non-zero if initialized */
} tiku_cached_region_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — CACHE                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Create a cached region over an NVM address.
 *
 * Registers the SRAM buffer as a write-back cache and copies the current NVM
 * contents in, so the working copy starts in sync.  The region joins the global
 * table that tiku_cache_flush_all() walks.
 *
 * @param region     Cache descriptor to initialize
 * @param fram_addr  NVM address to cache
 * @param sram_buf   Caller-provided SRAM buffer (must be >= size bytes)
 * @param size       Size of the region in bytes (must be > 0)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad args,
 *         TIKU_MEM_ERR_FULL if the global region table is full
 */
tiku_mem_err_t tiku_cache_create(tiku_cached_region_t *region,
                                  uint8_t *fram_addr,
                                  uint8_t *sram_buf,
                                  tiku_mem_arch_size_t size);

/**
 * @brief Get a pointer to the SRAM working copy.
 *
 * Returns the SRAM cache pointer and marks the region dirty, so the next
 * flush writes it back.  A caller that only reads need not flush.
 *
 * @param region  Cache descriptor (must be active)
 * @return Pointer to the SRAM working copy, or NULL if invalid
 */
void *tiku_cache_get(tiku_cached_region_t *region);

/**
 * @brief Mark a cached region as dirty.
 *
 * tiku_cache_get() marks the region dirty itself; this is needed after
 * writing through a pointer obtained before the last flush, which cleared
 * the flag.
 *
 * @param region  Cache descriptor (must be active)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if region is
 *         NULL or not active
 */
tiku_mem_err_t tiku_cache_mark_dirty(tiku_cached_region_t *region);

/**
 * @brief Flush a single cached region back to its NVM backing store.
 *
 * Copies the working copy out if dirty, bracketing the write in an unlock
 * window and clearing the dirty flag afterwards.  A no-op when clean.
 *
 * @param region  Cache descriptor to flush
 * @return TIKU_MEM_OK on success (including not-dirty no-op),
 *         TIKU_MEM_ERR_INVALID if region is NULL or not active,
 *         TIKU_MEM_ERR_IO when the flush at relock fails (the region stays
 *         dirty)
 */
tiku_mem_err_t tiku_cache_flush(tiku_cached_region_t *region);

/**
 * @brief Flush all registered cached regions.
 *
 * Iterates the global region table and flushes every dirty region, opening
 * the NVM window once for the whole batch.  On a failed relock no region is
 * marked clean.
 *
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_IO when the flush at relock
 *         fails
 * @note Call before a sleep that loses SRAM.
 */
tiku_mem_err_t tiku_cache_flush_all(void);

/**
 * @brief Reload a cached region from NVM into SRAM.
 *
 * Overwrites the SRAM working copy with the current NVM contents and clears
 * the dirty flag, discarding unflushed changes.  Used when a DMA transfer or
 * another external update modified the NVM.
 *
 * @param region  Cache descriptor to reload
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if region
 *         is NULL or not active
 */
tiku_mem_err_t tiku_cache_reload(tiku_cached_region_t *region);

/**
 * @brief Destroy a cached region and remove it from the global table.
 *
 * Does not flush: a caller that wants the changes kept calls
 * tiku_cache_flush() first.
 *
 * @param region  Cache descriptor to destroy
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if region
 *         is NULL or not active
 */
tiku_mem_err_t tiku_cache_destroy(tiku_cached_region_t *region);

/**
 * @brief Get the number of registered cached regions.
 *
 * Returns the current count of active entries in the global cache
 * table. Used by the hibernate layer to iterate all regions.
 *
 * @return Number of registered regions (0 to TIKU_CACHE_MAX_REGIONS)
 */
tiku_mem_arch_size_t tiku_cache_get_count(void);

/**
 * @brief Get a cached region by index.
 *
 * Returns a pointer to the cached region at the given index in the
 * global table. Used by the hibernate layer to reload all regions
 * on warm resume.
 *
 * @param index  Index into the global cache table
 * @return Pointer to the cached region, or NULL if index is out of range
 */
tiku_cached_region_t *tiku_cache_get_region(tiku_mem_arch_size_t index);

/*---------------------------------------------------------------------------*/
/* PROCESS MEMORY CONTEXT                                                    */
/*---------------------------------------------------------------------------*/

/*
 * Per-process isolated memory contexts.
 *
 * A process memory context binds an SRAM scratch arena, an NVM arena, an
 * optional HIFRAM arena and a set of cached regions to one process
 * identifier.  tiku_proc_alloc() picks the arena for the requested tier, and
 * the arena bounds-checks the allocation; under the cooperative scheduler no
 * other process runs in between.
 *
 * Usage:
 *   static tiku_proc_mem_t pmem;
 *   tiku_proc_mem_create(&pmem, 1, TIKU_MEM_AUTO, 64, 128);
 *   void *p = tiku_proc_alloc(&pmem, TIKU_MEM_SRAM, 16);
 *   tiku_proc_mem_destroy(&pmem);
 */

/** Maximum number of cached regions a process context can own */
#ifndef TIKU_PROC_MEM_MAX_CACHES
#define TIKU_PROC_MEM_MAX_CACHES  4
#endif

/**
 * @brief Per-process memory context.
 *
 * Binds an SRAM scratch arena, an NVM arena, an optional HIFRAM arena and a
 * set of cached regions to a process identifier.  The process allocates
 * through this context, so each allocation lands in its own arenas.
 */
typedef struct {
    uint8_t               pid;          /**< Owning process identifier */
    tiku_arena_t          sram_arena;   /**< Process's SRAM scratch space */
    tiku_arena_t          nvm_arena;    /**< Process's persistent storage */
    tiku_arena_t          hifram_arena; /**< HIFRAM bulk space, inactive until
                                         *   tiku_proc_mem_attach_hifram() */
    tiku_cached_region_t *caches[TIKU_PROC_MEM_MAX_CACHES];
                                        /**< Process's cached regions */
    uint8_t               cache_count;  /**< Number of attached caches */
    uint8_t               active;       /**< Non-zero if context is live */
#if TIKU_MEM_RECLAIM_ENABLE
    tiku_mem_owner_t       owner;        /**< Registered process owner */
    uint16_t              owner_key_base; /**< First of three owner keys */
#endif
} tiku_proc_mem_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — PROCESS MEMORY CONTEXT                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Create an isolated memory context for a process.
 *
 * Allocates an SRAM arena and an NVM arena from the tier allocator,
 * both bound to the given process identifier. Either size may be zero
 * to skip that tier.  If the NVM arena fails, the SRAM arena is released.
 *
 * @param pmem       Context to initialize
 * @param pid        Owning process identifier (used as arena id)
 * @param tier       AUTO puts each arena in its own tier; any other tier
 *                   holds both
 * @param sram_size  SRAM arena capacity in bytes (0 to skip)
 * @param nvm_size   NVM arena capacity in bytes (0 to skip)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad args or both
 *         sizes zero, TIKU_MEM_ERR_BUSY while one of its arenas holds a
 *         tracked reservation, or the tier allocator's error
 */
tiku_mem_err_t tiku_proc_mem_create(tiku_proc_mem_t *pmem,
                                     uint8_t pid,
                                     tiku_mem_tier_t tier,
                                     tiku_mem_arch_size_t sram_size,
                                     tiku_mem_arch_size_t nvm_size);

#if TIKU_MEM_RECLAIM_ENABLE
/**
 * @brief Bind an existing context's arenas to a registered process owner.
 *
 * The SRAM, NVM and HIFRAM arenas take the stable keys @p key_base,
 * @p key_base + 1 and @p key_base + 2.  A context with attached caches is
 * refused.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for bad arguments, an owner that
 *         is not a registered process, or an arena that cannot be owned;
 *         TIKU_MEM_ERR_BUSY while the owner or a span is held by reclaim
 * @note Keep cache and snapshot control blocks outside the owned arenas.
 */
tiku_mem_err_t tiku_proc_mem_set_owner(tiku_proc_mem_t *, tiku_mem_owner_t,
                                       uint16_t key_base);

/**
 * @brief tiku_proc_mem_create() for a registered process owner.
 *
 * The arenas take keys as in tiku_proc_mem_set_owner().
 *
 * @note During a restore, an initializer failure is left to the owner's
 *       cleanup and held-slot retry; do not create another context over the
 *       partial claims.
 */
tiku_mem_err_t tiku_proc_mem_create_owned(tiku_proc_mem_t *, uint8_t pid,
    tiku_mem_tier_t, tiku_mem_arch_size_t sram_size, tiku_mem_arch_size_t nvm_size,
    tiku_mem_owner_t, uint16_t key_base);
#endif

/**
 * @brief Destroy a process memory context.
 *
 * Flushes and destroys all attached cached regions, then resets and
 * releases the SRAM, NVM and HIFRAM arenas. After this call the context
 * is inactive and all memory previously allocated through it is invalid.
 *
 * @param pmem  Context to destroy
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if pmem is NULL
 *         or not active, TIKU_MEM_ERR_BUSY while a reclaim job holds its
 *         owner, or the first cache or arena error
 * @note Stop every user of the context's memory first; it is not wiped.
 */
tiku_mem_err_t tiku_proc_mem_destroy(tiku_proc_mem_t *pmem);

/**
 * @brief Attach a HIFRAM arena to an existing process context.
 *
 * A lazy opt-in, separate from tiku_proc_mem_create().  Where the tier is
 * unavailable it returns TIKU_MEM_ERR_NOMEM.
 *
 * @param pmem  Active process memory context
 * @param size  HIFRAM arena capacity in bytes
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad arguments or
 *         when a HIFRAM arena is already attached, or the tier allocator's
 *         error
 */
tiku_mem_err_t tiku_proc_mem_attach_hifram(tiku_proc_mem_t *pmem,
                                            tiku_mem_arch_size_t size);

/**
 * @brief Allocate within a process context (bounds-checked).
 *
 * Picks the arena for the requested tier.  AUTO prefers HIFRAM when attached
 * and the request is large enough, then directly writable local SRAM/HIFRAM.
 * Protected NVM requires an explicit request, regardless of the arena name.
 *
 * @param pmem  Active process memory context
 * @param tier  Memory tier (SRAM, NVM, HIFRAM, or AUTO)
 * @param size  Bytes requested (must be > 0)
 * @return Pointer to the allocated memory, or NULL on failure
 */
void *tiku_proc_alloc(tiku_proc_mem_t *pmem,
                       tiku_mem_tier_t tier,
                       tiku_mem_arch_size_t size);

/**
 * @brief Attach a cached region to a process context.
 *
 * Adds an already-created cached region to the process context so
 * that tiku_proc_mem_destroy() will flush and destroy it automatically.
 *
 * @param pmem    Active process memory context
 * @param region  Cached region to attach (must be active)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_FULL if the process
 *         has reached TIKU_PROC_MEM_MAX_CACHES, TIKU_MEM_ERR_INVALID on bad
 *         arguments or a context with a reclaim owner
 */
tiku_mem_err_t tiku_proc_mem_attach_cache(tiku_proc_mem_t *pmem,
                                           tiku_cached_region_t *region);

/**
 * @brief Get statistics for a process arena.
 *
 * @param pmem   Active process memory context
 * @param tier   Which arena to query (SRAM, NVM or HIFRAM; not AUTO)
 * @param stats  Output statistics
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad args,
 *         TIKU_MEM_ERR_NOT_FOUND for HIFRAM when no arena is attached
 */
tiku_mem_err_t tiku_proc_mem_stats(const tiku_proc_mem_t *pmem,
                                    tiku_mem_tier_t tier,
                                    tiku_mem_stats_t *stats);

/*---------------------------------------------------------------------------*/
/* HIBERNATE / RESUME                                                        */
/*---------------------------------------------------------------------------*/

/*
 * Hibernate/resume orchestration for the memory subsystem.
 *
 * Before entering deep sleep (LPMx.5 on MSP430) all dirty write-back
 * caches must be flushed to NVM and a hibernate marker written so
 * the next boot can distinguish a warm resume from a cold start.
 *
 * Usage: call tiku_mem_hibernate(nvm_buf, now) before the sleep.  On every
 * boot, after tiku_mem_init(), tiku_mem_resume(nvm_buf, &marker) returns
 * TIKU_MEM_OK for a warm resume, with the cached regions reloaded, and
 * TIKU_MEM_ERR_NOT_FOUND for a cold boot.
 */

/** Key used in the persist store for the hibernate marker */
#define TIKU_HIBERNATE_KEY       "hib"

/** Magic number to validate hibernate markers (ASCII "THIB") */
#define TIKU_HIBERNATE_MAGIC     0x54484942U

/**
 * @brief Hibernate marker, kept in NVM through the persist store.
 *
 * Contains a magic number for corruption detection, a monotonic
 * boot count incremented on each hibernate, and a caller-supplied
 * timestamp for diagnostics.
 */
typedef struct {
    uint32_t magic;       /**< TIKU_HIBERNATE_MAGIC if valid */
    uint32_t boot_count;  /**< Monotonic hibernate cycle counter */
    uint32_t timestamp;   /**< Caller-supplied timestamp */
    uint32_t crc;         /**< CRC-32 over boot_count and timestamp; a torn
                               marker fails it at resume */
} tiku_hibernate_marker_t;

/**
 * @brief Prepare the memory subsystem for hibernation.
 *
 * Flushes all dirty write-back caches to NVM and writes a hibernate
 * marker (boot count + timestamp) to the persist store.
 *
 * @param fram_buf   NVM buffer for the marker (>= sizeof marker)
 * @param timestamp  Caller-supplied timestamp value
 * @return TIKU_MEM_OK on success, or an error code
 * @note Call before entering any deep sleep that loses SRAM.
 */
tiku_mem_err_t tiku_mem_hibernate(uint8_t *fram_buf, uint32_t timestamp);

/**
 * @brief Check for warm resume after hibernation.
 *
 * A valid marker reloads every registered cached region and reports a warm
 * resume; its absence is a cold boot.
 *
 * @param fram_buf    NVM buffer used for the hibernate marker
 * @param marker_out  Output: hibernate marker (may be NULL)
 * @return TIKU_MEM_OK if warm resume, TIKU_MEM_ERR_NOT_FOUND if cold boot,
 *         or another error code when the store cannot be set up
 * @note Call after tiku_mem_init() on every boot.
 */
tiku_mem_err_t tiku_mem_resume(uint8_t *fram_buf,
                                tiku_hibernate_marker_t *marker_out);

/**
 * @brief Reset the hibernate subsystem to uninitialised state.
 *
 * Clears SRAM registration only. A valid NVM marker remains readable,
 * and its boot count continues on the next hibernate.
 *
 * @note Test use only.
 */
void tiku_mem_hibernate_reset(void);

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — MODULE                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the memory management module.
 *
 * Brings up the region registry, then the platform's memory setup (which
 * restores a mirrored durable image), then MPU NVM protection, and then, with
 * the cell table, moves persist cells to this image's layout.
 *
 * @note Runs at boot before any other memory-subsystem call.
 */
void tiku_mem_init(void);

#endif /* TIKU_MEM_H_ */
