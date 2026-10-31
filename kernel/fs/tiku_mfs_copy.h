/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mfs_copy.h - Bounded object relocation for segment compaction.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MFS_COPY_H_
#define TIKU_MFS_COPY_H_
#include "tiku_mfs.h"
/** @brief One caller-owned relocation job; keep the filesystem exclusively serialized. */
typedef struct {
    uint64_t reader, id, offset, length;
    uint8_t active;
} tiku_mfs_copy_t;
/** @brief Pin a completed object and begin a private replacement with its digest. */
int tiku_mfs_copy_begin(tiku_mfs_t *, tiku_mfs_copy_t *, const char *name);
/**
 * @brief Copy and commit at most capacity bytes, then publish after the final byte.
 * @return 1 while more data remains, zero after publication, or a negative error.
 * @note capacity is 1..65536. The caller supplies the transfer buffer. Whole-segment
 * collection after relocation releases segments no retained root still references.
 */
int tiku_mfs_copy_step(tiku_mfs_t *, tiku_mfs_copy_t *, void *buffer, uint32_t capacity);
/** @brief Drop a relocation's private upload and release its pinned source. */
int tiku_mfs_copy_abort(tiku_mfs_t *, tiku_mfs_copy_t *);
#endif
