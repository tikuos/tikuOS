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
    uint8_t parked;             /**< a read-only session left open, idle */
    uint64_t parked_at;         /**< tiku_s31_time() when it was parked */
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
#if TIKU_S31_SD_ASYNC_ENABLE
/** @brief Caller-owned media session; retain it and its backend until close completes. */
typedef struct {
    tiku_s31_sd_async_t transport;
    tiku_s31_mfs_block_t *backend;
    uint8_t *output;
    uint64_t lba;
    uint32_t count, completed;
    uint8_t phase;
} tiku_s31_mfs_async_t;
/** @brief Begin identification of a CID-bound read-only session; return 1 accepted. */
int tiku_s31_mfs_async_acquire(tiku_s31_mfs_async_t *, tiku_s31_mfs_block_t *);
/** @brief Begin 1..8 extent-relative sector reads on an acquired session.
 * @note Retain output through completion or close. Completed sectors can remain
 * in output after a later sector fails. No write extent is enabled.
 */
int tiku_s31_mfs_async_read(tiku_s31_mfs_async_t *, uint64_t lba, uint32_t count, void *output);
/** @brief Enable writes within the CID-checked session's configured extent. */
int tiku_s31_mfs_async_write_enable(tiku_s31_mfs_async_t *);
/** @brief Begin 1..8 authorized sector writes; retain input until completion or close. */
int tiku_s31_mfs_async_write(tiku_s31_mfs_async_t *, uint64_t lba, uint32_t count, const void *input);
/** @brief Begin a programming barrier and card-status check on a writable session. */
int tiku_s31_mfs_async_sync(tiku_s31_mfs_async_t *);
/** @brief Advance one transport phase; return 1 pending, 0 complete or an MFS error.
 * @note Successful operations retain the session for its next operation.
 * Transport errors close the session. Invalid-call refusals retain ownership;
 * BUSY at acquisition leaves it unaccepted.
 */
int tiku_s31_mfs_async_poll(tiku_s31_mfs_async_t *);
/** @brief Cancel pending work and release the card and its pads.
 * @note BUSY retains the session and borrowed buffers. Once a write data token
 * is sent, repeated close calls complete that sector before releasing ownership.
 */
int tiku_s31_mfs_async_close(tiku_s31_mfs_async_t *);
/**
 * @brief End a session, leaving an idle read-only one open on its backend.
 * @note An idle, CID-checked session that is not write-enabled is parked: the
 * card stays initialized and its pads claimed, and the next acquire reads its
 * CID again instead of power-cycling it; a card that fails that read is
 * identified afresh. Any other session is closed as tiku_s31_mfs_async_close()
 * closes it.
 */
int tiku_s31_mfs_async_park(tiku_s31_mfs_async_t *);
/** @brief Close a parked session; 0 when none remains, BUSY or IO otherwise. */
int tiku_s31_mfs_async_unpark(tiku_s31_mfs_block_t *);
/** @brief Close a session parked for at least @p ticks of tiku_s31_time(). */
int tiku_s31_mfs_async_expire(tiku_s31_mfs_block_t *, uint64_t ticks);
#endif
#endif
