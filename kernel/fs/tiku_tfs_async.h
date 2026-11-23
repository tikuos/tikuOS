/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tfs_async.h - Caller-owned file replacement and rename with polled
 * durable writes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TFS_ASYNC_H_
#define TIKU_TFS_ASYNC_H_

#include "tiku_tfs.h"

#ifndef TIKU_TFS_ASYNC_ENABLE
/** @brief Compile the polled replacement API when set to 1. */
#define TIKU_TFS_ASYNC_ENABLE 0
#endif

/** @brief Cancellation result before the directory commit has been submitted. */
#define TIKU_TFS_ASYNC_CANCELLED (-12)

/** @brief Backend writes borrow input until poll retires them, without waiting in either callback.
 * @note begin returns 1 accepted, TFS_ERR_BUSY unaccepted, another negative error unaccepted.
 * poll returns 1/TFS_ERR_BUSY pending, 0 durable, another negative error retired.
 * @note Aligned four-byte writes are atomic, including across power loss.
 * Success updates the backend's mapped bytes before returning. Errors may follow a commit.
 * Pending writes leave mapped reads usable.
 * The backend owns NVM protection, cache state, serialization and recovery until retirement.
 */
typedef struct {
    int (*begin)(void *, tiku_nvm_backend_t *, size_t offset, const void *, size_t length);
    int (*poll)(void *);
} tiku_tfs_async_ops_t;

/** @brief Zero-initialized replacement state with borrowed immutable input.
 * @note Retain the state, input, ops and backend context through completion.
 */
typedef struct {
    tiku_tfs_wr_t writer;
    const tiku_tfs_async_ops_t *ops;
    void *context;
    const uint8_t *data;
    size_t length, chunk;
    uint32_t word, old_run;
    unsigned dir, source, target;
    uint8_t record[4u + TIKU_TFS_NAME_MAX];
    int result;
    uint8_t phase, pending, cancelled, committing, replacing, renaming;
} tiku_tfs_async_t;

/** @brief Reserve a whole-file replacement without I/O; return 0 accepted or a TFS error.
 * @note Supports nonrotating stores; rotating stores return TFS_ERR_INVAL.
 * Serialize calls and exclude mount/init/format or direct backend access until completion.
 * The normal writer interlock excludes other file mutations; reads retain their usual contract.
 */
int tiku_tfs_async_begin(tiku_tfs_t *, tiku_tfs_async_t *, const char *name,
                         const void *data, size_t length, const tiku_tfs_async_ops_t *, void *context);

/** @brief Begin a rename of @p from to @p to without I/O, replacing a file named @p to;
 * return 0 accepted or the error tiku_tfs_rename() returns.
 * @note Writes as tiku_tfs_rename() does, one backend request each; a rename to its own
 * name completes at once with result 0.  Supports nonrotating stores, with the interlock
 * and serialization of tiku_tfs_async_begin().  The commit is the target's run word or a
 * new entry's gate; the source's clear follows it, so cancellation is refused from the
 * commit on.  TFS_ERR_IO leaves the store unmounted; tiku_tfs_mount() then finds the old
 * names, or the new one wherever the commit landed.
 */
int tiku_tfs_async_rename(tiku_tfs_t *, tiku_tfs_async_t *, const char *from, const char *to,
                          const tiku_tfs_async_ops_t *, void *context);

/** @brief Begin a delete of @p name without I/O; return 0 accepted or the error
 * tiku_tfs_delete() returns.
 * @note One request, the entry's gate, which commits, so cancellation is refused once it is
 * issued; the file's slots are free on success.  Serialization as tiku_tfs_async_begin().
 */
int tiku_tfs_async_delete(tiku_tfs_t *, tiku_tfs_async_t *, const char *name,
                          const tiku_tfs_async_ops_t *, void *context);

/** @brief The port's polled writes to its store backend, and their context in @p context.
 * @note A port that defines none gets writes through the backend's own write, each
 * durable when its begin returns; the S31 defines one over its sector journal.
 */
const tiku_tfs_async_ops_t *tiku_tfs_async_port(void **context);

/** @brief Advance one memory phase or one backend callback; 1 pending, 0 done, negative misuse.
 * @note A backend request holds one metadata record, or payload up to the end of an erase
 * sector (TIKU_TFS_SECT), or of a 256-byte block where the medium has no erase unit.
 * Read result only after completion: 0 durable, CANCELLED, or TFS_ERR_IO requiring remount.
 */
int tiku_tfs_async_step(tiku_tfs_async_t *);

/** @brief Request cancellation; BUSY means the commit is submitted and must complete normally.
 * @note An accepted cancel still requires step until completion before releasing memory.
 */
int tiku_tfs_async_cancel(tiku_tfs_async_t *);
#endif
