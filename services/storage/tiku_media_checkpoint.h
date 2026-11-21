/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_checkpoint.h - queued checkpoint progression over polled writes.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_CHECKPOINT_H_
#define TIKU_MEDIA_CHECKPOINT_H_
#include "tiku_media_store.h"
#include <kernel/fs/tiku_mfs_checkpoint.h>
/** @brief Nonblocking write session; acquire/write/sync return 1 on acceptance.
 * @note Acquire validates the configured medium and enables its authorized
 * extent. BUSY admits no operation. Poll returns 1 pending, 0 complete or a
 * negative error; errors retire the session except ARG, which retains it.
 * Close returns zero after retirement; other results retain ownership.
 */
typedef struct {
    int (*acquire)(void *,void *backend);
    int (*write)(void *,uint64_t lba,const void *sector);
    int (*sync)(void *);
    int (*poll)(void *);
    int (*close)(void *);
} tiku_media_write_ops_t;
/** @brief Caller-owned queue lease and checkpoint buffers. */
typedef struct {
    tiku_mfs_checkpoint_t checkpoint;
    tiku_media_store_t *store;
    tiku_media_request_t *request;
    const tiku_media_write_ops_t *ops;
    void *context;
    int result;
    uint8_t phase,opened;
} tiku_media_checkpoint_t;
/** @brief Initialize an unused owner with transport callbacks. */
void tiku_media_checkpoint_init(tiku_media_checkpoint_t *,const tiku_media_write_ops_t *,void *);
/** @brief Advance one phase; 0 idle, 1 progress, 2 unsupported head, negative refusal.
 * @note Accepted CHECKPOINT requests own store serialization until transport
 * retirement. Queue admission remains available for other clients.
 */
int tiku_media_checkpoint_step(tiku_media_checkpoint_t *,tiku_media_store_t *);
/** @brief Cancel after transport retirement; BUSY preserves the request lease. */
int tiku_media_checkpoint_cancel(tiku_media_checkpoint_t *,tiku_media_store_t *,tiku_media_request_t *);
#endif
