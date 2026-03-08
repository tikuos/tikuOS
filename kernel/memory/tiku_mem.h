/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem.h - Memory management module
 *
 * TikuOS memory management for microcontrollers with small SRAM (2-8 KB).
 *
 * Two allocator types are provided:
 *
 *   Arena (bump-pointer) — for groups of allocations that share a
 *   lifetime. O(1) alloc, O(1) bulk free. Zero per-object metadata.
 *   No individual free.
 *
 *   Pool (fixed-size block) — for individual alloc/free of equal-sized
 *   objects. O(1) alloc, O(1) free. Zero per-block metadata via an
 *   embedded freelist. Zero fragmentation.
 *
 * Why arenas:
 *   malloc/free on small SRAM leads to fragmentation that is fatal on
 *   microcontrollers with no MMU. An arena allocates by advancing a
 *   pointer — O(1) with zero per-object metadata — and frees everything
 *   at once by resetting the pointer. Ideal for temporary buffers during
 *   packet processing, sensor data batching, or any scenario where a
 *   group of allocations is used and discarded together.
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

#ifndef TIKU_MEM_H_
#define TIKU_MEM_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>
#include "hal/tiku_mem_hal.h"
#include "hal/tiku_mpu_hal.h"

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
    TIKU_MEM_ERR_FULL   = -3,   /**< Store is full (no free slots) */
    TIKU_MEM_ERR_NOT_FOUND = -4 /**< Key not found in store */
} tiku_mem_err_t;

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
    tiku_mem_arch_size_t total_bytes;  /**< Total capacity of the backing buffer */
    tiku_mem_arch_size_t used_bytes;   /**< Currently allocated bytes */
    tiku_mem_arch_size_t peak_bytes;   /**< High-water mark (lifetime maximum) */
    tiku_mem_arch_size_t alloc_count;  /**< Number of successful allocations */
} tiku_mem_stats_t;

/*---------------------------------------------------------------------------*/
/* ARENA ALLOCATOR                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Arena (bump-pointer) allocator control block
 *
 * An arena manages a contiguous caller-provided buffer. Each allocation
 * advances an offset forward. There is no individual free — all
 * allocations are discarded at once via tiku_arena_reset().
 *
 * Fields:
 *   buf       – pointer to the start of the backing buffer
 *   capacity  – total size of the backing buffer in bytes
 *   offset    – current allocation position (next free byte)
 *   peak      – highest offset ever reached (survives reset)
 *   count     – number of successful allocations since last reset
 *   id        – user-assigned identifier for debugging
 *   active    – non-zero if the arena has been initialized
 */
typedef struct {
    uint8_t              *buf;       /**< Backing buffer (caller-provided) */
    tiku_mem_arch_size_t  capacity;  /**< Buffer size in bytes */
    tiku_mem_arch_size_t  offset;    /**< Current bump-pointer position */
    tiku_mem_arch_size_t  peak;      /**< Lifetime high-water mark */
    tiku_mem_arch_size_t  count;     /**< Allocations since last reset */
    uint8_t               id;        /**< Arena identifier for debugging */
    uint8_t               active;    /**< Non-zero if initialized */
} tiku_arena_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — ARENA                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize an arena over a caller-provided buffer
 *
 * Sets up the arena control block. The buffer must be provided by the
 * caller (typically a static array). The arena does not own the buffer.
 *
 * @param arena    Arena control block to initialize
 * @param buf      Pointer to the backing buffer
 * @param size     Size of the backing buffer in bytes
 * @param id       User-assigned identifier (0-255)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if arena or buf is NULL
 */
tiku_mem_err_t tiku_arena_create(tiku_arena_t *arena, uint8_t *buf,
                                 tiku_mem_arch_size_t size, uint8_t id);

/**
 * @brief Allocate memory from an arena
 *
 * Bump-pointer allocation. The returned pointer is aligned to the
 * platform's native word boundary (TIKU_MEM_ARCH_ALIGNMENT). Requests
 * that are not a multiple of the alignment are rounded up internally.
 *
 * There is no individual free. Use tiku_arena_reset() to reclaim all
 * allocations at once.
 *
 * @param arena    Arena to allocate from
 * @param size     Number of bytes requested (must be > 0)
 * @return Pointer to the allocated memory, or NULL if the arena is full
 *         or the arguments are invalid
 */
void *tiku_arena_alloc(tiku_arena_t *arena, tiku_mem_arch_size_t size);

/**
 * @brief Reset an arena, reclaiming all allocations
 *
 * Sets the offset back to zero. Does not zero the buffer memory —
 * this keeps reset O(1) regardless of arena size. The peak high-water
 * mark is preserved across resets for lifetime tracking.
 *
 * @param arena    Arena to reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if arena is NULL
 */
tiku_mem_err_t tiku_arena_reset(tiku_arena_t *arena);

/**
 * @brief Securely reset an arena, zeroing all memory before reclaiming
 *
 * Same as tiku_arena_reset() but first overwrites the entire buffer
 * with zeros. Use this when the arena held sensitive data such as
 * cryptographic keys, nonces, or credentials that must not linger
 * in SRAM (physical probing, memory-dump bugs) or in NVM (persists
 * across reboots).
 *
 * A volatile pointer is used for the zeroing loop to prevent the
 * compiler from optimizing out the memset — a known problem in
 * security-sensitive code (the compiler sees that the memory is never
 * read after zeroing and may elide the entire operation).
 *
 * CPU-cycle penalty vs tiku_arena_reset():
 *   tiku_arena_reset()        — O(1), ~6 cycles regardless of size.
 *   tiku_arena_secure_reset() — O(n), approximately 3-5 cycles per
 *       byte depending on the target platform. For typical arena
 *       sizes:
 *         64 B   ~  320 cycles
 *        256 B   ~ 1280 cycles
 *       2048 B   ~10240 cycles
 *       Only use when the security benefit justifies the cost.
 *
 * @param arena    Arena to securely reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if arena is NULL
 */
tiku_mem_err_t tiku_arena_secure_reset(tiku_arena_t *arena);

/**
 * @brief Get current statistics for an arena
 *
 * Fills a tiku_mem_stats_t with a snapshot of the arena's state.
 *
 * @param arena    Arena to query
 * @param stats    Output structure (caller-provided)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if either pointer is NULL
 */
tiku_mem_err_t tiku_arena_stats(const tiku_arena_t *arena,
                                tiku_mem_stats_t *stats);

/*---------------------------------------------------------------------------*/
/* POOL ALLOCATOR                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Debug poisoning for pool allocator
 *
 * When TIKU_POOL_DEBUG is non-zero, tiku_pool_free() writes 0xDE to
 * all bytes of a freed block (after the freelist pointer). This makes
 * use-after-free bugs immediately visible in memory dumps and often
 * causes a crash rather than silent corruption.
 *
 * Disable in production to avoid the per-free overhead.
 */
#ifndef TIKU_POOL_DEBUG
#define TIKU_POOL_DEBUG  0
#endif

/**
 * @brief Fixed-size block pool allocator control block
 *
 * A pool manages a contiguous caller-provided buffer divided into
 * equal-sized blocks. Free blocks form an embedded freelist — each
 * free block stores a pointer to the next free block inside its own
 * memory, so there is zero per-block metadata overhead.
 *
 * When a block is allocated, its entire memory is available to the
 * caller. When freed, the first sizeof(void *) bytes are repurposed
 * as the next-pointer in the freelist.
 *
 * Fields:
 *   buf         – pointer to the start of the backing buffer
 *   block_size  – aligned size of each block in bytes
 *   block_count – total number of blocks in the pool
 *   free_head   – head of the embedded freelist (NULL if pool is empty)
 *   used_count  – number of blocks currently allocated
 *   peak_count  – lifetime high-water mark of used_count
 *   id          – user-assigned identifier for debugging
 *   active      – non-zero if the pool has been initialized
 */
typedef struct {
    uint8_t              *buf;         /**< Backing buffer (caller-provided) */
    tiku_mem_arch_size_t  block_size;  /**< Aligned block size in bytes */
    tiku_mem_arch_size_t  block_count; /**< Total number of blocks */
    void                 *free_head;   /**< Head of embedded freelist */
    tiku_mem_arch_size_t  used_count;  /**< Currently allocated blocks */
    tiku_mem_arch_size_t  peak_count;  /**< Lifetime high-water mark */
    uint8_t               id;          /**< Pool identifier for debugging */
    uint8_t               active;      /**< Non-zero if initialized */
} tiku_pool_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — POOL                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a pool over a caller-provided buffer
 *
 * Divides the buffer into block_count blocks of block_size bytes each
 * and chains them into an embedded freelist. The block_size is rounded
 * up to TIKU_MEM_ARCH_ALIGNMENT and clamped to a minimum of
 * sizeof(void *) (also aligned), since each free block must hold the
 * freelist pointer.
 *
 * The pool does not own the buffer — the caller provides a statically
 * allocated array. The buffer must be at least
 * aligned_block_size * block_count bytes.
 *
 * @param pool         Pool control block to initialize
 * @param buf          Pointer to the backing buffer
 * @param block_size   Requested size of each block in bytes
 * @param block_count  Number of blocks
 * @param id           User-assigned identifier (0-255)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if pool or buf
 *         is NULL or block_count is 0
 */
tiku_mem_err_t tiku_pool_create(tiku_pool_t *pool, uint8_t *buf,
                                 tiku_mem_arch_size_t block_size,
                                 tiku_mem_arch_size_t block_count,
                                 uint8_t id);

/**
 * @brief Allocate a block from the pool
 *
 * Pops the head of the embedded freelist. O(1) — no search, no
 * fragmentation. Tracks used_count and peak_count.
 *
 * @param pool   Pool to allocate from (must be active)
 * @return Pointer to the allocated block, or NULL if the pool is empty
 *         or the arguments are invalid
 */
void *tiku_pool_alloc(tiku_pool_t *pool);

/**
 * @brief Return a block to the pool
 *
 * Pushes the block back onto the freelist head. O(1). Validates that
 * ptr falls within the pool's buffer range and is aligned to a block
 * boundary — returns TIKU_MEM_ERR_INVALID if not. This catches
 * common bugs: freeing a pointer from a different allocator, freeing
 * a stack pointer, or freeing at the wrong offset.
 *
 * When TIKU_POOL_DEBUG is enabled, the freed block is poisoned with
 * 0xDE bytes (after the freelist pointer) to catch use-after-free.
 *
 * @param pool   Pool the block belongs to
 * @param ptr    Pointer previously returned by tiku_pool_alloc
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if ptr is
 *         outside the pool or not aligned to a block boundary
 */
tiku_mem_err_t tiku_pool_free(tiku_pool_t *pool, void *ptr);

/**
 * @brief Get current statistics for a pool
 *
 * Fills a tiku_mem_stats_t with a snapshot of the pool's state.
 * Fields are mapped as: total_bytes = block_size * block_count,
 * used_bytes = block_size * used_count, peak_bytes = block_size *
 * peak_count, alloc_count = used_count.
 *
 * @param pool    Pool to query
 * @param stats   Output structure (caller-provided)
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if either
 *         pointer is NULL
 */
tiku_mem_err_t tiku_pool_stats(const tiku_pool_t *pool,
                                tiku_mem_stats_t *stats);

/**
 * @brief Reset the pool, returning all blocks to the freelist
 *
 * Re-chains all blocks into the freelist and resets used_count to
 * zero. The peak high-water mark is preserved across resets for
 * lifetime tracking. O(n) in block_count.
 *
 * @param pool   Pool to reset
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if pool is NULL
 */
tiku_mem_err_t tiku_pool_reset(tiku_pool_t *pool);

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
 * @brief One entry in the persistent store
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
 * @brief Persistent store control block
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
 * @brief Initialize the persistent store, recovering valid entries
 *
 * Scans all slots: entries with correct magic and valid flag are kept,
 * all others are cleared. Call once at boot.
 *
 * @param store   Store to initialize
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if store is NULL
 */
tiku_mem_err_t tiku_persist_init(tiku_persist_store_t *store);

/**
 * @brief Register an NVM buffer under a key
 *
 * If the key already exists, updates the NVM pointer but preserves
 * existing data (survives firmware updates). Otherwise allocates the
 * first empty slot.
 *
 * @param store     Store to register into
 * @param key       Null-terminated key string
 * @param fram_buf  Pointer to caller-provided NVM buffer
 * @param capacity  Size of the NVM buffer in bytes
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID, or TIKU_MEM_ERR_FULL
 */
tiku_mem_err_t tiku_persist_register(tiku_persist_store_t *store,
                                     const char *key,
                                     uint8_t *fram_buf,
                                     tiku_mem_arch_size_t capacity);

/**
 * @brief Read a value from the persistent store into an SRAM buffer
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
 * @brief Write a value from SRAM into the persistent NVM store
 *
 * Copies data into the NVM buffer via the HAL, updates value_len,
 * and increments write_count for wear monitoring. Does not unlock
 * NVM write-protection internally — the caller is expected to batch
 * writes within an unprotected region.
 *
 * @param store     Store to write into
 * @param key       Key to look up
 * @param data      Source data in SRAM
 * @param data_len  Length of source data
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_NOT_FOUND, TIKU_MEM_ERR_NOMEM,
 *         or TIKU_MEM_ERR_INVALID
 */
tiku_mem_err_t tiku_persist_write(tiku_persist_store_t *store,
                                   const char *key,
                                   const uint8_t *data,
                                   tiku_mem_arch_size_t data_len);

/**
 * @brief Delete an entry from the persistent store
 *
 * Clears the entry slot with memset so the key can no longer be found.
 *
 * @param store   Store to delete from
 * @param key     Key to delete
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_NOT_FOUND, or TIKU_MEM_ERR_INVALID
 */
tiku_mem_err_t tiku_persist_delete(tiku_persist_store_t *store,
                                    const char *key);

/**
 * @brief Check wear level for a key
 *
 * Returns the write count and whether it exceeds the warning threshold.
 * NVM technologies have finite write endurance (tracking matters for
 * safety-critical systems and hot keys).
 *
 * @param store       Store to query
 * @param key         Key to check
 * @param write_count Output: number of writes to this key (may be NULL)
 * @return 1 if write_count exceeds threshold, 0 if within limits,
 *         or a negative tiku_mem_err_t on error
 */
int tiku_persist_wear_check(tiku_persist_store_t *store,
                             const char *key,
                             uint32_t *write_count);

/*---------------------------------------------------------------------------*/
/* MPU (MEMORY PROTECTION UNIT)                                              */
/*---------------------------------------------------------------------------*/

/*
 * NVM write-protection via the platform MPU (HAL layer).
 *
 * Default policy: all MPU segments are read+execute, no write.
 * This prevents stray pointers and runaway code from corrupting NVM.
 *
 * To perform an intentional NVM write the caller must explicitly
 * unlock, write, and relock:
 *
 *   uint16_t saved = tiku_mpu_unlock_nvm();
 *   // ... write to NVM ...
 *   tiku_mpu_lock_nvm(saved);
 *
 * For convenience, tiku_mpu_scoped_write() wraps the full sequence
 * and disables interrupts for the duration. Interrupts are disabled
 * because an ISR that fires while NVM is unlocked could itself write
 * to NVM through a bug, defeating the protection.
 */

/**
 * @brief MPU segment identifiers
 *
 * The platform MPU divides the address space into three segments.
 */
typedef enum {
    TIKU_MPU_SEG1 = 0,
    TIKU_MPU_SEG2 = 1,
    TIKU_MPU_SEG3 = 2
} tiku_mpu_seg_t;

/**
 * @brief MPU permission flags
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
 * @brief Initialize the MPU with default NVM protection
 *
 * Sets all three segments to read+execute (no write) via the HAL
 * and enables the MPU. Called early in boot before any other
 * subsystem runs.
 */
void tiku_mpu_init(void);

/**
 * @brief Set permissions on a single MPU segment
 *
 * @param seg    Segment to configure (0-2)
 * @param perm   Permission flags (combination of TIKU_MPU_* values)
 */
void tiku_mpu_set_permissions(tiku_mpu_seg_t seg, tiku_mpu_perm_t perm);

/**
 * @brief Unlock NVM for writing on all segments
 *
 * Adds write permission to all segments via the arch layer. Returns
 * an opaque saved state so it can be restored by tiku_mpu_lock_nvm().
 *
 * @return Previous MPU state value
 */
uint16_t tiku_mpu_unlock_nvm(void);

/**
 * @brief Restore MPU state after an NVM write
 *
 * @param saved_state  Value returned by a prior tiku_mpu_unlock_nvm()
 */
void tiku_mpu_lock_nvm(uint16_t saved_state);

/**
 * @brief Execute a function with NVM unlocked, interrupts disabled
 *
 * Disables interrupts, unlocks NVM, calls fn(ctx), relocks NVM,
 * and re-enables interrupts. The write function must be short to
 * avoid missing interrupts for too long.
 *
 * @param fn   Function to call while NVM is writable
 * @param ctx  Opaque context pointer passed to fn
 */
void tiku_mpu_scoped_write(tiku_mpu_write_fn fn, void *ctx);

/**
 * @brief Enable NMI on MPU violation instead of device reset
 *
 * On platforms where the default MPU violation response is a reset,
 * this function switches to an NMI instead, allowing the violation
 * to be detected and handled without losing state. Must be called
 * before any intentional violation testing.
 */
void tiku_mpu_enable_violation_nmi(void);

/**
 * @brief Read MPU violation flags
 *
 * Returns per-segment violation flags: bit 0 = segment 1,
 * bit 1 = segment 2, bit 2 = segment 3. A non-zero return
 * means at least one segment access violation was detected.
 *
 * @return Violation flags (bits [2:0])
 */
uint16_t tiku_mpu_get_violation_flags(void);

/**
 * @brief Clear all MPU violation flags
 *
 * Resets all segment violation flags to zero so the next
 * violation can be detected cleanly.
 */
void tiku_mpu_clear_violation_flags(void);

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — MODULE                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the memory management module
 *
 * Entry point for the memory subsystem. Activates MPU NVM protection
 * first, then performs platform-specific memory hardware setup.
 */
void tiku_mem_init(void);

#endif /* TIKU_MEM_H_ */
