/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_mutation.h - serialized file mutations with polled transport leases.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_MUTATION_H_
#define TIKU_MEDIA_MUTATION_H_
#include "tiku_media_store.h"
#include <kernel/fs/tiku_mfs_mutation.h>
/** @brief Polled read/write transport over an authorized medium extent.
 * @note Acquire/read/write/sync return 1 accepted, BUSY without admission, or a
 * negative error. Acquire validates identity and enables writes. Poll returns
 * 1 pending, zero completed, or an error that retires the session except ARG,
 * which retains it. Close returns zero after retirement; other results retain
 * ownership. Callbacks do not wait for hardware. Buffers remain borrowed.
 */
typedef struct {
    int (*acquire)(void *,void *backend);
    int (*read)(void *,uint64_t lba,void *sector);
    int (*write)(void *,uint64_t lba,const void *sector);
    int (*sync)(void *);
    int (*poll)(void *);
    int (*close)(void *);
} tiku_media_mutation_ops_t;
/** @brief Queue owner; mutation storage is supplied separately and retained. */
typedef struct {
    tiku_mfs_mutation_t *mutation;
    tiku_media_store_t *store;
    tiku_media_request_t *request;
    const tiku_media_mutation_ops_t *ops;
    void *context;
    int result;
    uint8_t phase,opened,begun;
} tiku_media_mutation_t;
/** @brief Initialize an unused owner with initialized mutation storage and transport. */
void tiku_media_mutation_init(tiku_media_mutation_t *,tiku_mfs_mutation_t *,const tiku_media_mutation_ops_t *,void *);
/** @brief Advance one queue phase; 0 idle, 1 progress, 2 unsupported head, negative refusal.
 * @note Namespace, complete-file and streaming writer requests own filesystem
 * serialization through transport retirement. Other clients may enqueue.
 * BEGIN uses id as the expected destination ID and returns the upload ID.
 */
int tiku_media_mutation_step(tiku_media_mutation_t *,tiku_media_store_t *);
/** @brief Cancel a queued request after transport retirement; BUSY retains its lease.
 * @note A transaction already published remains committed. Interrupted writes
 * or allocation freeze the mount; remount determines the published namespace.
 */
int tiku_media_mutation_cancel(tiku_media_mutation_t *,tiku_media_store_t *,tiku_media_request_t *);
#endif
