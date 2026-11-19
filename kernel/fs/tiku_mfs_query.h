/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_mfs_query.h - Caller-owned, sector-paced catalog and snapshot queries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MFS_QUERY_H_
#define TIKU_MFS_QUERY_H_
#include "tiku_mfs.h"
#ifndef TIKU_MEDIA_ASYNC_ENABLE
#define TIKU_MEDIA_ASYNC_ENABLE 0
#endif
#if TIKU_MEDIA_ASYNC_ENABLE && !TIKU_MEDIA_NAMES_ENABLE
#error "Resumable media queries require namespace versions"
#endif
/** @brief Query state and one borrowed-sector destination, retained through completion.
 * @note Serialize the filesystem from begin through completion/cancellation.
 * While waiting is set, read sector_lba into sector and call supply exactly once.
 * Other fields are internal. Results are valid when done is set: result is zero
 * for enumeration, a byte count for reads, or a negative MFS error.
 */
typedef struct {
    tiku_mfs_object_t object;
    tiku_mfs_link_t link;
    tiku_mfs_ref_t ref;
    uint64_t generation, sequence, names_epoch, rank, position, id, base, chunk, offset, sector_lba;
    uint32_t total, copied, frame_length, footer_crc, slice_length, slice_filled, slice_crc, expected_crc;
    uint8_t *output;
    char lower[64], found[64];
    uint8_t sector[512], slice[4096];
    int result, io_result;
    uint8_t phase, mode, exclusive, depth, level, waiting, have, done, pinned;
} tiku_mfs_query_t;
/** @brief Begin a name lower-bound query without block I/O; expected zero selects a new view. */
int tiku_mfs_query_enumerate(tiku_mfs_t *, tiku_mfs_query_t *, const char *lower, int exclusive, uint64_t expected);
/** @brief Begin a read of at most 65536 bytes from an already pinned reader without block I/O.
 * @note Output remains borrowed until completion. Validated slices can be copied
 * before a later slice fails. Keep the reader pinned throughout the query.
 */
int tiku_mfs_query_read(tiku_mfs_t *, tiku_mfs_query_t *, uint64_t token, uint64_t offset, void *, uint32_t length);
/** @brief Advance one metadata page or at most 512 checksum bytes; return 1 pending, 0 done.
 * @note A completed payload slice copies at most 4096 validated bytes to output.
 */
int tiku_mfs_query_step(tiku_mfs_t *, tiku_mfs_query_t *);
/** @brief Supply a requested sector's zero/negative I/O result; reject duplicate or unsolicited supply. */
int tiku_mfs_query_supply(tiku_mfs_query_t *, int result);
/** @brief Pin the completed enumeration result without rereading metadata; return its reader token. */
int tiku_mfs_query_open(tiku_mfs_t *, tiku_mfs_query_t *, uint64_t *token);
#endif
