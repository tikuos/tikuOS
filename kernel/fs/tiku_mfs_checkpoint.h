/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_mfs_checkpoint.h - sector-paced checkpoint publication.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MFS_CHECKPOINT_H_
#define TIKU_MFS_CHECKPOINT_H_
#include "tiku_mfs_query.h"
/** @brief Requested operation; sector storage remains borrowed until supply. */
enum { TIKU_MFS_CHECKPOINT_WRITE=1, TIKU_MFS_CHECKPOINT_SYNC=2 };
/** @brief Caller-owned checkpoint state; fields other than I/O and result are internal.
 * @note Serialize the filesystem from begin until completion. While waiting is
 * nonzero, write sector to sector_lba or issue a barrier, then supply its result.
 */
typedef struct {
    uint64_t generation, sequence, epoch, base, sector_lba;
    size_t offset;
    uint32_t checksum;
    uint8_t sector[512];
    int result, io_result;
    uint8_t phase, waiting, have, done, changed;
} tiku_mfs_checkpoint_t;
/** @brief Begin without block access; refuse collection, failed mounts and epoch exhaustion. */
int tiku_mfs_checkpoint_begin(tiku_mfs_t *, tiku_mfs_checkpoint_t *);
/** @brief Advance at most one sector/checksum phase; return 1 pending, 0 done. */
int tiku_mfs_checkpoint_step(tiku_mfs_t *, tiku_mfs_checkpoint_t *);
/** @brief Complete one requested write/barrier with zero or a negative result. */
int tiku_mfs_checkpoint_supply(tiku_mfs_checkpoint_t *, int result);
/** @brief Cancel after transport retirement; BUSY refuses an outstanding I/O lease.
 * @note Cancellation after issuing a write freezes the mount until remount,
 * because a new checkpoint header may have reached the card.
 */
int tiku_mfs_checkpoint_cancel(tiku_mfs_t *, tiku_mfs_checkpoint_t *);
#endif
