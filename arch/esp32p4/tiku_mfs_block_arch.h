/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mfs_block_arch.h - CID-bound media filesystem block sessions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_MFS_BLOCK_H_
#define TIKU_P4_MFS_BLOCK_H_
#include "tiku_sdcard_arch.h"
#include <kernel/fs/tiku_block.h>
/** @brief Caller-owned card identity and authorized partition extent. */
typedef struct {
    tiku_p4_sdcard_t card;
    tiku_block_t block;
    uint64_t first;
    uint32_t cid[4], hz;
    uint8_t active;
} tiku_p4_mfs_block_t;
/** @brief Configure an extent without touching hardware; CID must come from a prior probe. */
int tiku_p4_mfs_block_init(tiku_p4_mfs_block_t *, uint64_t first, uint64_t sectors,
                          const uint32_t cid[4], uint32_t hz);
/** @brief Acquire the shared host and verify CID before permitting extent writes. */
int tiku_p4_mfs_block_acquire(void *);
/** @brief Quiesce the card and restore the C6 host after one storage operation. */
int tiku_p4_mfs_block_release(void *);
#endif
