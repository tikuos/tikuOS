/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_store.h - Queued media operations and client ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_STORE_H_
#define TIKU_MEDIA_STORE_H_
#include <kernel/fs/tiku_mfs.h>
#include <kernel/fs/tiku_mfs_copy.h>
/** @brief Bounded request queue depth. */
#define TIKU_MEDIA_QUEUE 4u
/** @brief Trusted caller authorization bits, checked before requests enter the queue. */
#define TIKU_MEDIA_READ 1u
#define TIKU_MEDIA_WRITE 2u
#define TIKU_MEDIA_ADMIN 4u
/** @brief Storage operations performed by one scheduler-context owner. */
typedef enum {
    TIKU_MEDIA_MOUNT, TIKU_MEDIA_FORMAT, TIKU_MEDIA_BEGIN,
    TIKU_MEDIA_RESUME, TIKU_MEDIA_APPEND, TIKU_MEDIA_SYNC,
    TIKU_MEDIA_FINALIZE, TIKU_MEDIA_RELEASE, TIKU_MEDIA_DELETE,
    TIKU_MEDIA_OPEN, TIKU_MEDIA_READ_AT, TIKU_MEDIA_CLOSE,
    TIKU_MEDIA_STAT, TIKU_MEDIA_LIST, TIKU_MEDIA_CHECKPOINT,
    TIKU_MEDIA_COLLECT_BEGIN, TIKU_MEDIA_COLLECT_STEP,
    TIKU_MEDIA_COPY_BEGIN, TIKU_MEDIA_COPY_STEP, TIKU_MEDIA_COPY_ABORT,
    TIKU_MEDIA_UPLOADS, TIKU_MEDIA_SEEK
} tiku_media_op_t;
/**
 * @brief Caller-owned request, including borrowed buffers and result storage.
 * @note Keep this object, name and buffer unchanged until done is set. The caller
 * maps external capabilities to permissions; client is a nonzero ownership ID.
 * Submit and step run in serialized scheduler context, outside capture interrupts.
 */
typedef struct {
    tiku_media_op_t op;
    uint32_t client, permissions, length;
    uint64_t id, offset;
    const char *name;
    void *buffer;
    tiku_mfs_object_t *info;
    uint8_t digest[32];
    tiku_mfs_policy_t policy;
    tiku_mfs_cursor_t cursor;
    int result;
    uint8_t done;
} tiku_media_request_t;
/** @brief Caller-owned service state and optional hardware-session hooks. */
typedef struct {
    tiku_mfs_t *fs;
    tiku_block_t block;
    void *used,*mark;
    size_t capacity;
    uint8_t uuid[16];
    void *backend;
    int (*acquire)(void *);
    int (*release)(void *);
    tiku_media_request_t *queue[TIKU_MEDIA_QUEUE];
    uint32_t writer, collector;
    tiku_mfs_copy_t copy;
    struct {uint64_t token;uint32_t client;} readers[TIKU_MFS_READERS];
    unsigned count;
    uint8_t running, failed;
} tiku_media_store_t;
/** @brief Bind storage and optional paired session hooks without mounting or formatting. */
int tiku_media_store_init(tiku_media_store_t *, tiku_mfs_t *, const tiku_block_t *,
                          const uint8_t uuid[16], void *used, void *mark, size_t capacity,
                          void *backend, int (*acquire)(void *), int (*release)(void *));
/** @brief Enqueue a request; BUSY leaves it unaccepted and caller-owned. */
int tiku_media_store_submit(tiku_media_store_t *, tiku_media_request_t *);
/** @brief Complete at most one request and release the hardware before setting done. */
int tiku_media_store_step(tiku_media_store_t *);
#endif
