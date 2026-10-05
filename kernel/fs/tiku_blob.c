/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_blob.c - large objects as chunked files in the /data store.
 *
 * Builds the chunk and manifest names and stores each one through the TFS
 * API; the file has no direct NVM access and no platform code.  tiku_blob.h
 * describes the layout and the write order.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_blob.h"
#include <kernel/memory/tiku_nvm_mirror.h>   /* tiku_nvm_crc32() */

/*---------------------------------------------------------------------------*/
/* MANIFEST                                                                  */
/*---------------------------------------------------------------------------*/

#define BLOB_MAGIC    0x424C4F42u   /* 'BLOB' */
#define BLOB_VERSION  1u

/*
 * Stored as a plain struct.  /data can outlive the image that wrote it, so a
 * later build must read the same layout: six u32 fields have no padding on
 * any target, and every target is little-endian.
 */
typedef struct {
    uint32_t magic;     /**< BLOB_MAGIC                                     */
    uint32_t version;   /**< BLOB_VERSION                                   */
    uint32_t total;     /**< payload bytes                                  */
    uint32_t chunk;     /**< bytes per chunk (the last one may be shorter)  */
    uint32_t chunks;    /**< number of chunk files                          */
    uint32_t crc;       /**< CRC32 over the whole payload                   */
} blob_mnf_t;

/*---------------------------------------------------------------------------*/
/* NAMES                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Build "<base>.mnf" (idx < 0) or "<base>.NNN" into @p out.
 *
 * @return 0, or -1 if @p base is NULL, empty or longer than
 *         TIKU_BLOB_NAME_MAX.
 */
static int
blob_name(char out[TIKU_TFS_NAME_MAX], const char *base, int idx)
{
    size_t n = 0u;

    if (base == NULL) {
        return -1;
    }
    while (base[n] != '\0') {
        if (n >= (size_t)TIKU_BLOB_NAME_MAX) {
            return -1;                       /* too long to carry a suffix */
        }
        out[n] = base[n];
        n++;
    }
    if (n == 0u) {
        return -1;                           /* empty base name            */
    }
    out[n++] = '.';
    if (idx < 0) {
        out[n++] = 'm'; out[n++] = 'n'; out[n++] = 'f';
    } else {
        out[n++] = (char)('0' + ((unsigned)idx / 100u) % 10u);
        out[n++] = (char)('0' + ((unsigned)idx / 10u) % 10u);
        out[n++] = (char)('0' + (unsigned)idx % 10u);
    }
    out[n] = '\0';
    return 0;
}

/**
 * @brief Blob code for a failed store call.
 *
 * A missing file is NOENT, a full store SPACE and a corrupt entry CRC; any
 * other refusal (a backend fault, a busy or unmounted store) is IO.
 */
static int
blob_tfs_err(int rc)
{
    if (rc == TFS_ERR_NOTFOUND) {
        return TIKU_BLOB_ERR_NOENT;
    }
    if (rc == TFS_ERR_NOSPACE || rc == TFS_ERR_TOOBIG) {
        return TIKU_BLOB_ERR_SPACE;
    }
    if (rc == TFS_ERR_CORRUPT) {
        return TIKU_BLOB_ERR_CRC;
    }
    return TIKU_BLOB_ERR_IO;
}

/** @brief Read and validate the manifest; TIKU_BLOB_OK or a blob error. */
static int
blob_read_mnf(tiku_tfs_t *fs, const char *name, blob_mnf_t *m)
{
    char   nm[TIKU_TFS_NAME_MAX];
    size_t got = 0u;
    int    rc;

    if (blob_name(nm, name, -1) != 0) {
        return TIKU_BLOB_ERR_PARAM;
    }
    rc = tiku_tfs_read(fs, nm, m, sizeof *m, &got);
    if (rc != TFS_OK) {
        return blob_tfs_err(rc);
    }
    if (got != sizeof *m || m->magic != BLOB_MAGIC ||
        m->version != BLOB_VERSION || m->chunk == 0u ||
        m->chunks > TIKU_BLOB_CHUNK_MAX) {
        return TIKU_BLOB_ERR_CRC;            /* present but not a blob     */
    }
    /*
     * chunks must equal what total and chunk imply.  Callers iterate on chunks
     * and size each copy as total - off, which underflows for an inflated
     * count and writes whole chunks past a buffer sized for total.
     */
    if (m->chunks != (uint32_t)((m->total + m->chunk - 1u) / m->chunk)) {
        return TIKU_BLOB_ERR_CRC;            /* inconsistent manifest      */
    }
    return TIKU_BLOB_OK;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC                                                                    */
/*---------------------------------------------------------------------------*/

int
tiku_blob_store(tiku_tfs_t *fs, const char *name, const void *src, size_t len)
{
    const uint8_t *p = (const uint8_t *)src;
    char       nm[TIKU_TFS_NAME_MAX];
    blob_mnf_t m;
    size_t     off;
    unsigned   i, chunks;
    int        rc;

    if (fs == NULL || name == NULL || (src == NULL && len != 0u)) {
        return TIKU_BLOB_ERR_PARAM;
    }
    if (blob_name(nm, name, -1) != 0) {
        return TIKU_BLOB_ERR_PARAM;
    }
    chunks = (unsigned)((len + TIKU_BLOB_CHUNK - 1u) / TIKU_BLOB_CHUNK);
    if (chunks > TIKU_BLOB_CHUNK_MAX) {
        return TIKU_BLOB_ERR_SPACE;
    }

    /* The old manifest goes first: from here until the new one is written
     * the blob does not exist, and no manifest stands over chunks it does
     * not describe.  A missing manifest is a first store; any other refusal
     * returns with the old blob whole. */
    rc = tiku_tfs_delete(fs, nm);
    if (rc != TFS_OK && rc != TFS_ERR_NOTFOUND) {
        return blob_tfs_err(rc);
    }

    /*
     * Delete every chunk at or above the new count before writing.  The write
     * loop touches only 0..chunks-1 and tiku_blob_delete() walks the
     * manifest's count, so this sweep frees the surplus of a larger previous
     * blob and the tail of a store that was cut partway.
     *
     * Chunk indices are written densely from 0 and deleted from the top down,
     * so a cut never leaves a gap: the first index that is not present is the
     * end.  The manifest is already gone, so nothing visible depends on these.
     */
    for (i = chunks; i < TIKU_BLOB_CHUNK_MAX; i++) {
        size_t stale = 0u;
        if (blob_name(nm, name, (int)i) != 0) {
            break;
        }
        if (tiku_tfs_stat(fs, nm, &stale) != TFS_OK) {
            break;                           /* dense naming: this is the end */
        }
    }
    while (i > chunks) {
        i--;
        if (blob_name(nm, name, (int)i) == 0) {
            (void)tiku_tfs_delete(fs, nm);
        }
    }

    for (i = 0u, off = 0u; i < chunks; i++, off += TIKU_BLOB_CHUNK) {
        size_t this_len = len - off;
        if (this_len > TIKU_BLOB_CHUNK) {
            this_len = TIKU_BLOB_CHUNK;
        }
        if (blob_name(nm, name, (int)i) != 0) {
            return TIKU_BLOB_ERR_PARAM;
        }
        rc = tiku_tfs_write(fs, nm, p + off, this_len);
        if (rc != TFS_OK) {
            return blob_tfs_err(rc);
        }
    }

    m.magic   = BLOB_MAGIC;
    m.version = BLOB_VERSION;
    m.total   = (uint32_t)len;
    m.chunk   = (uint32_t)TIKU_BLOB_CHUNK;
    m.chunks  = chunks;
    m.crc     = (len != 0u) ? tiku_nvm_crc32(src, len) : 0u;

    /* Commit point: the manifest's own write is atomic (TFS gate-last), so
     * the blob becomes visible in one step, after every chunk is durable. */
    if (blob_name(nm, name, -1) != 0) {
        return TIKU_BLOB_ERR_PARAM;
    }
    rc = tiku_tfs_write(fs, nm, &m, sizeof m);
    if (rc != TFS_OK) {
        return blob_tfs_err(rc);
    }
    return TIKU_BLOB_OK;
}

int
tiku_blob_load(tiku_tfs_t *fs, const char *name,
               void *dst, size_t cap, size_t *out_len)
{
    uint8_t   *p = (uint8_t *)dst;
    char       nm[TIKU_TFS_NAME_MAX];
    blob_mnf_t m;
    size_t     off;
    unsigned   i;
    int        rc;

    if (fs == NULL || name == NULL || dst == NULL) {
        return TIKU_BLOB_ERR_PARAM;
    }
    rc = blob_read_mnf(fs, name, &m);
    if (rc != TIKU_BLOB_OK) {
        return rc;
    }
    if (out_len != NULL) {
        *out_len = (size_t)m.total;          /* set even when over cap     */
    }
    if ((size_t)m.total > cap) {
        return TIKU_BLOB_ERR_SPACE;
    }

    for (i = 0u, off = 0u; i < m.chunks; i++, off += (size_t)m.chunk) {
        size_t want = (size_t)m.total - off;
        size_t got  = 0u;
        if (want > (size_t)m.chunk) {
            want = (size_t)m.chunk;
        }
        if (blob_name(nm, name, (int)i) != 0) {
            return TIKU_BLOB_ERR_PARAM;
        }
        rc = tiku_tfs_read(fs, nm, p + off, want, &got);
        if (rc != TFS_OK) {
            return blob_tfs_err(rc);         /* a lost chunk is NOENT      */
        }
        if (got != want) {
            return TIKU_BLOB_ERR_CRC;        /* short chunk: torn store    */
        }
    }

    if (m.total != 0u && tiku_nvm_crc32(dst, (size_t)m.total) != m.crc) {
        return TIKU_BLOB_ERR_CRC;
    }
    return TIKU_BLOB_OK;
}

int
tiku_blob_stat(tiku_tfs_t *fs, const char *name, size_t *out_len)
{
    blob_mnf_t m;
    int        rc;

    if (fs == NULL || name == NULL || out_len == NULL) {
        return TIKU_BLOB_ERR_PARAM;
    }
    rc = blob_read_mnf(fs, name, &m);
    if (rc == TIKU_BLOB_OK) {
        *out_len = (size_t)m.total;
    }
    return rc;
}

int
tiku_blob_delete(tiku_tfs_t *fs, const char *name)
{
    char       nm[TIKU_TFS_NAME_MAX];
    blob_mnf_t m;
    unsigned   i;
    int        rc;

    if (fs == NULL || name == NULL) {
        return TIKU_BLOB_ERR_PARAM;
    }
    rc = blob_read_mnf(fs, name, &m);
    if (rc != TIKU_BLOB_OK) {
        return rc;
    }
    /* Manifest first: the blob stops existing at that single write, and the
     * chunk deletions that follow are pure space reclamation.  They run from
     * the last chunk down, so a cut leaves a run from 0 that the next store of
     * the same name reclaims: the chunks it overwrites, then its sweep. */
    if (blob_name(nm, name, -1) == 0) {
        rc = tiku_tfs_delete(fs, nm);
        if (rc != TFS_OK) {
            return blob_tfs_err(rc);         /* the blob is still whole    */
        }
    }
    for (i = m.chunks; i > 0u; i--) {
        if (blob_name(nm, name, (int)(i - 1u)) == 0) {
            (void)tiku_tfs_delete(fs, nm);
        }
    }
    return TIKU_BLOB_OK;
}
