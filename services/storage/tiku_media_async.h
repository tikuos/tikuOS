/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_async.h - Serialized read requests with polled backend retirement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_ASYNC_H_
#define TIKU_MEDIA_ASYNC_H_
#include "tiku_media_store.h"
#include <kernel/fs/tiku_mfs_query.h>
/** @brief Caller-owned read-only transport; accepted begin calls return 1.
 * @note acquire BUSY retains nothing. poll returns 1 pending, 0 complete;
 * ARG refuses a call with ownership retained, other errors close the session.
 * close returns zero after cancellation/release; errors retain ownership.
 * Calls must not wait for hardware. State and read output remain borrowed.
 */
typedef struct {
    int (*acquire)(void *,void *backend);
    int (*read)(void *,uint64_t lba,void *sector);
    int (*poll)(void *);
    int (*close)(void *);
} tiku_media_async_ops_t;
/** @brief One queue owner and query, stored in caller-provided memory without allocation. */
typedef struct {
    tiku_mfs_query_t query;
    tiku_media_store_t *store;
    tiku_media_request_t *request;
    const tiku_media_async_ops_t *ops;
    void *context;
    int result;
    uint8_t phase, opened;
} tiku_media_async_t;
/** @brief Bind an unused zeroed queue owner to a read-only transport. */
void tiku_media_async_init(tiku_media_async_t *,const tiku_media_async_ops_t *,void *context);
/** @brief Advance one read queue phase: 0 idle, 1 progress, 2 unsupported head, negative refusal.
 * @note ENUMERATE, OPEN_VERSION, READ_AT and CLOSE are supported. Other heads
 * remain queued for their existing owner; this function never executes writes.
 * A running query excludes synchronous stepping but permits bounded enqueue.
 */
int tiku_media_async_step(tiku_media_async_t *,tiku_media_store_t *);
/** @brief Cancel a matching queued request after closing any accepted transport.
 * @note Return zero when done, BUSY while close retains ownership. Call again
 * before releasing request storage. Cancellation completes with MFS_STALE.
 */
int tiku_media_async_cancel(tiku_media_async_t *,tiku_media_store_t *,tiku_media_request_t *);
#endif
