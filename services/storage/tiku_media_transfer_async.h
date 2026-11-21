/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_transfer_async.h - queued packet progression and borrowed input lifetime.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_TRANSFER_ASYNC_H_
#define TIKU_MEDIA_TRANSFER_ASYNC_H_
#include "tiku_media_transfer.h"
/** @brief Caller-owned packet control; the session holds staging and cached replies. */
typedef struct tiku_media_transfer_async tiku_media_transfer_async_t;
/** @brief Required packet-control storage size. */
size_t tiku_media_transfer_async_size(void);
/** @brief Required packet-control storage alignment. */
size_t tiku_media_transfer_async_align(void);
/** @brief Bind unused control/session storage without clearing the file-sized staging buffer.
 * @note Both objects and the mounted store outlive pending work. The caller wipes
 * reused session storage before initialization. No handles may remain owned by
 * client. Key is NULL for an unkeyed console or points to 32 immutable bytes. */
tiku_media_transfer_async_t *tiku_media_transfer_async_init(void *,size_t,tiku_media_transfer_t *,
                                                          tiku_media_store_t *,uint32_t client,const uint8_t *key);
/** @brief Borrow one packet; zero admits it, BUSY retains nothing, ARG rejects its size.
 * @note Input remains immutable through completion/cancellation and must not
 * overlap session or control storage. One packet can be pending. A completed
 * reply remains borrowed until the next successful submission or cancellation. */
int tiku_media_transfer_async_submit(tiku_media_transfer_async_t *,const void *,size_t);
/** @brief Advance one protocol phase, queue operation, or at most 64 checksum bytes.
 * @note Returns 1 pending or zero idle/completed. Copies at most 512 bytes per
 * payload/cache phase. The caller separately schedules the asynchronous media owners; this
 * function does not perform block I/O or synchronous store execution. */
int tiku_media_transfer_async_step(tiku_media_transfer_async_t *);
/** @brief Return the completed reply length, zero for a dropped frame, or BUSY while pending. */
int tiku_media_transfer_async_result(const tiku_media_transfer_async_t *,const uint8_t **reply);
/** @brief Return this packet's pending queue request, or NULL after its retirement. */
tiku_media_request_t *tiku_media_transfer_async_request(tiku_media_transfer_async_t *);
/** @brief Cancel after the pending queue request retires; BUSY retains borrowed input.
 * @note The caller cancels a returned request through its media owner first.
 * Cancellation invalidates reply retry state and authentication. Service-owned
 * handles remain for explicit client cleanup; completed publication is retained. */
int tiku_media_transfer_async_cancel(tiku_media_transfer_async_t *);
#endif
