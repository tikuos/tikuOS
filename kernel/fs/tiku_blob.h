/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_blob.h - large objects as chunked files in the /data store.
 *
 * Spans an object across numbered chunk files (<name>.000, .001, ...) described
 * by a <name>.mnf manifest.  The manifest is written last and deleted first, so
 * a power cut leaves the whole previous blob or none.  CRC32 checked on load.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLOB_H_
#define TIKU_BLOB_H_

#include <stddef.h>
#include <stdint.h>
#include <kernel/fs/tiku_tfs.h>

/** @brief Result codes (0 = success, negative = failure). */
typedef enum {
    TIKU_BLOB_OK        =  0,
    TIKU_BLOB_ERR_PARAM = -1,  /**< NULL argument, or a name that is too long */
    TIKU_BLOB_ERR_NOENT = -2,  /**< no manifest, or a chunk is missing        */
    TIKU_BLOB_ERR_SPACE = -3,  /**< destination too small, or store is full   */
    TIKU_BLOB_ERR_CRC   = -4,  /**< payload CRC mismatch (torn / corrupted)   */
    TIKU_BLOB_ERR_IO    = -5,  /**< the store refused a read or a write       */
} tiku_blob_err_t;

/**
 * @brief Longest base name, excluding the NUL.
 *
 * A chunk file is "<base>.NNN", so the base must leave room for four suffix
 * characters plus the terminator inside TIKU_TFS_NAME_MAX.
 */
#define TIKU_BLOB_NAME_MAX  (TIKU_TFS_NAME_MAX - 5u)

/** @brief Payload bytes per chunk (one whole store slot). */
#define TIKU_BLOB_CHUNK     ((size_t)TIKU_TFS_SLOT_DATA)

/** @brief Most chunks one blob may span (".000".."999"). */
#define TIKU_BLOB_CHUNK_MAX 1000u

/**
 * @brief Write @p len bytes as a chunked blob, replacing any previous one.
 *
 * Chunks are written first and the manifest last, so an interrupted store
 * leaves the blob absent rather than half-present.
 *
 * @return TIKU_BLOB_OK, or a negative tiku_blob_err_t.
 */
int tiku_blob_store(tiku_tfs_t *fs, const char *name,
                    const void *src, size_t len);

/**
 * @brief Load a blob into @p dst, verifying its CRC.
 *
 * @param cap      bytes available at @p dst.
 * @param out_len  receives the payload length (may be NULL).
 * @return TIKU_BLOB_OK, or a negative tiku_blob_err_t (ERR_SPACE if the
 *         blob is larger than @p cap -- @p out_len still receives its size).
 */
int tiku_blob_load(tiku_tfs_t *fs, const char *name,
                   void *dst, size_t cap, size_t *out_len);

/** @brief Payload length of a stored blob, without reading its chunks. */
int tiku_blob_stat(tiku_tfs_t *fs, const char *name, size_t *out_len);

/** @brief Remove a blob (manifest first, then every chunk). */
int tiku_blob_delete(tiku_tfs_t *fs, const char *name);

#endif /* TIKU_BLOB_H_ */
