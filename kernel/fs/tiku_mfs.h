/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mfs.h - Media objects, committed streams and versioned range readers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MFS_H_
#define TIKU_MFS_H_
#include "tiku_block.h"
#include <stddef.h>
#include <limits.h>
#if INT_MAX < 2147483647L
#error "The media filesystem requires a 32-bit int ABI"
#endif
/** @brief Format constants and bounded reader/traversal storage. */
#define TIKU_MFS_CATALOG_DEPTH 64u
#define TIKU_MFS_READERS 8u
#define TIKU_MFS_NAME 64u
#define TIKU_MFS_CHUNK 65536u
#define TIKU_MFS_SEGMENT 2048u
#define TIKU_MFS_JOURNAL 128u
/** @brief Result codes; IO freezes mutations until remount. */
typedef enum {
    TIKU_MFS_OK=0, TIKU_MFS_ARG=-1, TIKU_MFS_IO=-2,
    TIKU_MFS_CORRUPT=-3, TIKU_MFS_NOFS=-4, TIKU_MFS_SPACE=-5,
    TIKU_MFS_LIMIT=-6, TIKU_MFS_NOENT=-7, TIKU_MFS_BUSY=-8,
    TIKU_MFS_DIGEST=-9, TIKU_MFS_STALE=-10
} tiku_mfs_result_t;
/** @brief Writer publication policy. */
typedef enum { TIKU_MFS_STREAM=1, TIKU_MFS_ATOMIC=2 } tiku_mfs_policy_t;
/** @brief A checksummed sector reference; zero LBA denotes no reference. */
typedef struct { uint64_t lba; uint32_t crc; } tiku_mfs_ref_t;
/** @brief Incremental SHA-256 state, serialized with committed object metadata. */
typedef struct { uint32_t h[8]; uint8_t tail[64]; } tiku_mfs_hash_t;
/** @brief Object metadata and immutable extent root. Internal fields are not an ABI. */
typedef struct {
    uint64_t id, length, expected;
    tiku_mfs_ref_t root;
    char name[TIKU_MFS_NAME];
    uint8_t digest[32], expected_digest[32];
    tiku_mfs_hash_t hash;
    uint8_t height, policy, complete, visible;
} tiku_mfs_object_t;
/** @brief One snapshot reader pins its committed object root until closed. */
typedef struct { uint64_t token; tiku_mfs_object_t object; } tiku_mfs_reader_t;
/** @brief Persistent traversal cursor for bounded reclamation work. */
typedef struct {
    tiku_mfs_ref_t ref;
    uint64_t base;
    uint8_t level, child;
} tiku_mfs_walk_t;
/** @brief Checksummed catalog subtree with ranked visible and private counts. */
typedef struct {
    tiku_mfs_ref_t ref;
    uint64_t count, pending;
    uint8_t height;
} tiku_mfs_link_t;
/** @brief Zero-initialized listing cursor; mutations and remounts invalidate it. */
typedef struct {
    uint64_t generation, sequence, rank;
    uint8_t started, pending;
} tiku_mfs_cursor_t;
/** @brief Incremental catalog traversal state. */
typedef struct {tiku_mfs_link_t link; uint8_t stage;} tiku_mfs_catalog_walk_t;
/**
 * @brief Caller-owned mounted state; initialize once before mount/format.
 * @note All calls are serialized. Two caller-owned bitmap buffers each need
 * tiku_mfs_bitmap_bytes(block.sectors) bytes and must outlive the mount.
 * Do not inspect or modify internal fields while mounted.
 */
typedef struct {
    tiku_block_t block;
    tiku_mfs_object_t draft, gc_value;
    tiku_mfs_link_t ids, names;
    tiku_mfs_catalog_walk_t catalog_walk[TIKU_MFS_CATALOG_DEPTH];
    tiku_mfs_reader_t readers[TIKU_MFS_READERS];
    tiku_mfs_walk_t walk[12];
    uint8_t *used, *mark;
    size_t bitmap_bytes;
    uint8_t uuid[16];
    uint64_t sequence, epoch, next_id, next_token, tail, allocations, generation;
    uint32_t segments, bank_sectors, journal, cursor, free_segments;
    unsigned gc_reader, gc_depth, catalog_depth;
    uint8_t gc_phase, gc_active, catalog_write;
    uint8_t bank, mounted, failed, writing, collecting, recovered;
} tiku_mfs_t;
/** @brief Return bitmap storage size, or zero for unsupported geometry. */
size_t tiku_mfs_bitmap_bytes(uint64_t sectors);
/** @brief Read a version-2 whole-extent volume UUID from checked checkpoint headers. */
int tiku_mfs_identify(const tiku_block_t *, uint8_t uuid[16]);
/** @brief Initialize an unused context; readers must be closed before reinitializing. */
void tiku_mfs_init(tiku_mfs_t *fs);
/**
 * @brief Explicitly destroy an extent's format and create an empty media volume.
 * @note The caller supplies a new nonzero UUID and has authorized extent erasure.
 * This operation is not crash-atomic with the previous format.
 */
int tiku_mfs_format(tiku_mfs_t *, const tiku_block_t *, const uint8_t uuid[16],
                    void *used, void *mark, size_t bitmap_capacity);
/** @brief Recover a volume matching the expected UUID without writing to the medium. */
int tiku_mfs_mount(tiku_mfs_t *, const tiku_block_t *, const uint8_t uuid[16],
                   void *used, void *mark, size_t bitmap_capacity);
/** @brief Persist a catalog/allocator checkpoint; no payload scan is performed. */
int tiku_mfs_checkpoint(tiku_mfs_t *);
/**
 * @brief Begin one serialized writer; atomic objects require length and SHA-256.
 * @note Streams require an unused name. Atomic objects may replace a visible name
 * at finalize. Private atomic uploads occupy disk metadata and survive sync.
 */
int tiku_mfs_begin(tiku_mfs_t *, const char *, tiku_mfs_policy_t,
                   uint64_t expected, const uint8_t digest[32], uint64_t *id);
/** @brief Resume an incomplete object's last committed prefix by ID. */
int tiku_mfs_resume(tiku_mfs_t *, uint64_t id);
/**
 * @brief Append 1..65536 bytes to the active writer; input is released on return.
 * @note Success accepts bytes without promising durability. Partial last chunks
 * are rewritten with copy-on-write. Call sync/finalize for durable publication.
 */
int tiku_mfs_append(tiku_mfs_t *, const void *, uint32_t);
/** @brief Commit the active writer's prefix through two ordered barriers. */
int tiku_mfs_sync(tiku_mfs_t *);
/** @brief Verify expected length/digest and atomically publish a completed version. */
int tiku_mfs_finalize(tiku_mfs_t *);
/** @brief Release the writer and discard its unsynced tail; committed data remains. */
int tiku_mfs_release(tiku_mfs_t *);
/** @brief Delete a committed object by ID, including a private upload. */
int tiku_mfs_delete(tiku_mfs_t *, uint64_t id);
/** @brief Find a visible name and copy its committed metadata. */
int tiku_mfs_stat(tiku_mfs_t *, const char *, tiku_mfs_object_t *);
/** @brief Enumerate visible names in byte order; NOENT denotes the end, STALE a mutation. */
int tiku_mfs_list(tiku_mfs_t *, tiku_mfs_cursor_t *cursor, tiku_mfs_object_t *);
/** @brief Enumerate private incomplete uploads for administrative resume or deletion. */
int tiku_mfs_list_pending(tiku_mfs_t *, tiku_mfs_cursor_t *cursor, tiku_mfs_object_t *);
/** @brief Position a visible listing at its first name at or after a byte prefix. */
int tiku_mfs_list_seek(tiku_mfs_t *, const char *prefix, tiku_mfs_cursor_t *);
/** @brief Pin a visible version and return a mount-scoped reader token. */
int tiku_mfs_open_read(tiku_mfs_t *, const char *, uint64_t *token);
/** @brief Read a checked range, at most 65536 bytes; returns the copied byte count. */
int tiku_mfs_read_at(tiku_mfs_t *, uint64_t token, uint64_t offset, void *, uint32_t);
/** @brief Release a reader; an unknown or expired token returns STALE. */
int tiku_mfs_close_read(tiku_mfs_t *, uint64_t token);
/**
 * @brief Start exclusive reclamation after retiring older checkpoint roots.
 * @note Readers stay pinned; writers and new readers are refused during collection.
 */
int tiku_mfs_collect_begin(tiku_mfs_t *);
/**
 * @brief Visit at most budget index nodes, then publish reclaimed whole segments.
 * @return 1 while traversal remains, zero when complete, or a negative error.
 * @note Final publication writes two bounded checkpoint banks. Partially live
 * segments remain allocated; application-level object copying enables compaction.
 */
int tiku_mfs_collect_step(tiku_mfs_t *, unsigned budget);
#endif
