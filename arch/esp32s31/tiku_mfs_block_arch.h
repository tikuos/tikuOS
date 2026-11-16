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
#ifndef TIKU_S31_MFS_BLOCK_H_
#define TIKU_S31_MFS_BLOCK_H_
#include "tiku_sdcard_arch.h"
#include <kernel/fs/tiku_block.h>
/** @brief Caller-owned identity, authorized extent and card ownership state. */
typedef struct {
    tiku_s31_sdcard_t card;
    tiku_block_t block;
    uint64_t first;
    uint32_t cid[4], hz;
    uint8_t active;
} tiku_s31_mfs_block_t;
/** @brief Encode CID as four least-significant-word-first SD register words. */
void tiku_s31_sdcard_identity(const tiku_s31_sdcard_t *, uint32_t cid[4]);
/** @brief Initialize an unused context without hardware access, using a previously probed CID. */
int tiku_s31_mfs_block_init(tiku_s31_mfs_block_t *, uint64_t first, uint64_t sectors,
                           const uint32_t cid[4], uint32_t hz);
/** @brief Identify the card and verify identity and extent before enabling writes. */
int tiku_s31_mfs_block_acquire(void *);
/** @brief Quiesce pending programming and release the SPI bus and card pads. */
int tiku_s31_mfs_block_release(void *);
#endif
