/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_mfs_mutation.h - sector-paced namespace and content changes.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MFS_MUTATION_H_
#define TIKU_MFS_MUTATION_H_
#include "tiku_mfs_query.h"
/** @brief Opaque caller-owned traversal, transaction and sector storage. */
typedef struct tiku_mfs_mutation tiku_mfs_mutation_t;
/** @brief Requested sector operation, retained until supply. */
enum { TIKU_MFS_MUTATION_READ=1, TIKU_MFS_MUTATION_WRITE=2, TIKU_MFS_MUTATION_SYNC=3 };
/** @brief Required caller storage size; memory must be aligned for uint64_t. */
size_t tiku_mfs_mutation_size(void);
/** @brief Initialize unused caller storage, returning NULL for insufficient size/alignment. */
tiku_mfs_mutation_t *tiku_mfs_mutation_init(void *,size_t);
/** @brief Begin a checked rename, or delete when target is NULL, without block I/O.
 * @note Requires a completed visible file or empty directory matching ID and source, no active
 * writer, and no pending replacement at source. Rename refuses an occupied
 * target and file/directory conflicts. Source and target are copied. Serialize
 * the filesystem until completion or cancellation. Reuse only a retired owner.
 */
int tiku_mfs_mutation_begin(tiku_mfs_t *,tiku_mfs_mutation_t *,uint64_t id,const char *source,const char *target);
/** @brief Create or replace one complete file of at most TIKU_MFS_CHUNK bytes.
 * @note An expected ID of zero requires an absent name; a nonzero ID must match
 * the completed visible file. Pending uploads and namespace conflicts refuse
 * admission. The name is copied. Payload bytes remain immutable and borrowed
 * until completion or cancellation; NULL is valid for zero bytes. Payload must
 * not overlap operation storage or filesystem state. Publication
 * is atomic and existing readers retain their old object. Serialize the mount
 * as for rename. Files larger than this bounded replacement require streaming.
 */
int tiku_mfs_mutation_replace_begin(tiku_mfs_t *,tiku_mfs_mutation_t *,uint64_t expected_id,
                                  const char *name,const void *payload,uint32_t bytes);
/** @brief Begin a persistent upload with checked destination identity and no block I/O.
 * @note Zero expected_id requires an absent name. A nonzero ID permits atomic
 * replacement of that completed file. STREAM requires absence; ATOMIC requires
 * expected length and SHA-256. Completion owns the filesystem writer. */
int tiku_mfs_mutation_upload_begin(tiku_mfs_t *,tiku_mfs_mutation_t *,uint64_t expected_id,
                                  const char *name,tiku_mfs_policy_t,uint64_t expected,const uint8_t digest[32]);
/** @brief Resume an incomplete object by ID using paced metadata reads. */
int tiku_mfs_mutation_resume_begin(tiku_mfs_t *,tiku_mfs_mutation_t *,uint64_t id);
/** @brief Append 1..TIKU_MFS_CHUNK immutable borrowed bytes to the owned writer.
 * @note Steps hash at most 64 payload bytes or request one sector operation.
 * Partial old chunks are validated and copied. Success updates the draft;
 * flush commits its prefix. Cancellation after allocation requires remount. */
int tiku_mfs_mutation_append_begin(tiku_mfs_t *,tiku_mfs_mutation_t *,const void *,uint32_t);
/** @brief Commit a writer prefix, or verify and publish its final contents.
 * @note A nonzero final verifies the expected atomic digest and releases the
 * writer after publication. A zero final retains ownership. Serialize the mount
 * through completion and transport retirement as for other mutations. */
int tiku_mfs_mutation_flush_begin(tiku_mfs_t *,tiku_mfs_mutation_t *,int final);
#if TIKU_MEDIA_DIRECTORIES_ENABLE
/** @brief Explicitly enable persistent folders without changing file payloads.
 * @note Checkpoint current legacy roots, then retire the other legacy header
 * before permitting directory objects. A mixed-feature mount resumes from its
 * legacy bank. Failures or cancellation require remount. Completed migration
 * prevents older firmware from opening the volume. Requires mount serialization
 * and no active writer or collection; this operation does not format media. */
int tiku_mfs_mutation_upgrade_begin(tiku_mfs_t *,tiku_mfs_mutation_t *);
/** @brief Atomically rename or permanently delete a whole directory subtree.
 * @note A nonzero ID selects a typed directory; zero requires an implicit path
 * prefix with no exact object and at least one visible descendant. Target NULL
 * deletes the subtree. Pending uploads, occupied targets, self-descendants and
 * overlong mapped names refuse admission. The owner validates original metadata,
 * edits unpublished roots, then publishes once. Sector leases and cancellation
 * match single-object mutations. Serialize the mount through completion.
 */
int tiku_mfs_mutation_tree_begin(tiku_mfs_t *,tiku_mfs_mutation_t *,uint64_t id,const char *source,const char *target);
/** @brief Atomically copy a complete subtree to an absent destination, retaining its source.
 * @note Typed and implicit source selection matches tree_begin. Each file must
 * fit TIKU_MFS_CHUNK; the writable buffer must hold that many bytes and remain
 * borrowed through completion or cancellation. It must not overlap owner or
 * filesystem storage. The operation validates source slices and digests, assigns
 * new identities, then publishes once. Serialize the mount throughout the job. */
int tiku_mfs_mutation_tree_copy_begin(tiku_mfs_t *,tiku_mfs_mutation_t *,uint64_t id,
                                      const char *source,const char *target,void *buffer,uint32_t capacity);
/** @brief Read staged payload bytes and validated total; total is zero during preflight.
 * @note Prepared bytes are not visible until successful transaction publication. */
int tiku_mfs_mutation_progress(const tiku_mfs_mutation_t *,uint64_t *prepared,uint64_t *total);
/** @brief Create a persistent empty directory on a directory-enabled volume.
 * @note Requires an unused canonical relative path, no active writer and no
 * namespace conflict. Legacy volumes return UNSUPPORTED. Serialization, cancellation
 * and transport ownership match the other mutation operations. */
int tiku_mfs_mutation_mkdir_begin(tiku_mfs_t *,tiku_mfs_mutation_t *,const char *name);
#endif
/** @brief Advance one metadata phase, 64 hash bytes or one query slice; 1 pending, 0 done. */
int tiku_mfs_mutation_step(tiku_mfs_t *,tiku_mfs_mutation_t *);
/** @brief Return requested operation or zero; sector and LBA are borrowed until supply. */
int tiku_mfs_mutation_io(tiku_mfs_mutation_t *,uint64_t *lba,void **sector);
/** @brief Retire one requested operation with a zero or negative result. */
int tiku_mfs_mutation_supply(tiku_mfs_mutation_t *,int result);
/** @brief Return the completed result, or BUSY before completion. */
int tiku_mfs_mutation_result(const tiku_mfs_mutation_t *);
/** @brief Return the visible result ID after success; zero for deletion/error/pending. */
uint64_t tiku_mfs_mutation_id(const tiku_mfs_mutation_t *);
/** @brief Cancel after transport retirement; BUSY retains an outstanding I/O lease.
 * @note Once allocation or writes begin, interrupted operations freeze the
 * mount until remount. Cancellation does not undo a published transaction.
 */
int tiku_mfs_mutation_cancel(tiku_mfs_t *,tiku_mfs_mutation_t *);
#endif
