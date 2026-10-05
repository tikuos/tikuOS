/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bigblob.h - one very large object per slot, on erase-block media.
 *
 * For objects measured in tens of megabytes: written whole, read by pointer,
 * checked by CRC.  Header last, so a torn write leaves the slot absent.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BIGBLOB_H_
#define TIKU_BIGBLOB_H_

#include <stddef.h>
#include <stdint.h>
#include <kernel/fs/tiku_nvm_backend.h>

/*
 * A slot is one header erase block (magic, length, CRC, name) followed by the
 * payload.  There is no directory, free list or in-place update; many small
 * files belong in TFS or tiku_blob.
 */

/** @brief Result codes (0 = success, negative = failure). */
typedef enum {
    TIKU_BIGBLOB_OK        =  0,
    TIKU_BIGBLOB_ERR_PARAM = -1,  /**< NULL, len 0, unaligned or long name  */
    TIKU_BIGBLOB_ERR_NOENT = -2,  /**< the slot holds no blob               */
    TIKU_BIGBLOB_ERR_SPACE = -3,  /**< the blob does not fit the medium     */
    TIKU_BIGBLOB_ERR_CRC   = -4,  /**< the payload does not match its CRC   */
    TIKU_BIGBLOB_ERR_IO    = -5,  /**< write/erase refused or readback bad  */
} tiku_bigblob_err_t;

/** @brief Longest blob name, excluding the terminator. */
#define TIKU_BIGBLOB_NAME_MAX  23u

/**
 * @brief Bytes reserved for a slot's header: one 64 KB block, the largest the
 *        medium erases, so the payload starts on a block boundary and a
 *        slot's erases stay inside its own blocks.
 */
#define TIKU_BIGBLOB_HDR_BYTES 65536u

/** @brief What a slot holds, as reported by tiku_bigblob_info(). */
typedef struct {
    uint32_t len;                              /**< payload bytes          */
    uint32_t crc;                              /**< CRC32 over the payload */
    char     name[TIKU_BIGBLOB_NAME_MAX + 1u];
} tiku_bigblob_info_t;

/**
 * @brief A streamed write in progress, advanced by tiku_bigblob_step()
 *        between the caller's other work.  Fields are private.
 */
typedef struct {
    tiku_nvm_backend_t *be;
    const uint8_t      *src;
    uint32_t            slot_off;
    uint32_t            len;
    uint32_t            done;
    uint32_t            crc;
    char                name[TIKU_BIGBLOB_NAME_MAX + 1u];
    uint8_t             active;
} tiku_bigblob_wr_t;

/**
 * @brief Begin a streamed write; unpublishes the slot immediately.
 *
 * @param be       backend to write through
 * @param slot_off byte offset of the slot, erase-block aligned
 * @param name     blob name
 * @param src      payload, unchanged and valid until publication completes
 * @param len      payload bytes
 * @param w        receives the cursor
 * @return TIKU_BIGBLOB_OK, or a negative tiku_bigblob_err_t
 */
int tiku_bigblob_open(tiku_nvm_backend_t *be, uint32_t slot_off,
                      const char *name, const void *src, uint32_t len,
                      tiku_bigblob_wr_t *w);

/**
 * @brief Advance a streamed write by one step.
 *
 * A step erases and programs at most one 4 KB sector of payload.  The call
 * after the last sector reads the whole payload back to check its CRC, then
 * publishes the header.
 *
 * @param w    the cursor
 * @param done receives bytes written so far; may be NULL
 * @return 1 while more remains, 0 when published, negative on error
 */
int tiku_bigblob_step(tiku_bigblob_wr_t *w, uint32_t *done);

/**
 * @brief Write a blob into the slot at @p slot_off, replacing any previous.
 *
 * Does the whole write in one call; tiku_bigblob_open() and
 * tiku_bigblob_step() spread the same work over many.
 *
 * @param be       backend to write through
 * @param slot_off byte offset of the slot, erase-block aligned
 * @param name     blob name, up to TIKU_BIGBLOB_NAME_MAX characters
 * @param src      payload
 * @param len      payload bytes
 * @return TIKU_BIGBLOB_OK, or a negative tiku_bigblob_err_t
 */
int tiku_bigblob_write(tiku_nvm_backend_t *be, uint32_t slot_off,
                       const char *name, const void *src, uint32_t len);

/**
 * @brief Describe the blob in a slot without reading its payload.
 *
 * @param be       backend to read through
 * @param slot_off byte offset of the slot
 * @param out      receives the description
 * @return TIKU_BIGBLOB_OK, or a negative tiku_bigblob_err_t
 */
int tiku_bigblob_info(tiku_nvm_backend_t *be, uint32_t slot_off,
                      tiku_bigblob_info_t *out);

/**
 * @brief Pointer to the payload, straight into the mapped medium.
 *
 * @param be       backend to read through
 * @param slot_off byte offset of the slot
 * @param len      receives the payload length; may be NULL
 * @return the payload, or NULL when the slot holds nothing
 */
const void *tiku_bigblob_map(tiku_nvm_backend_t *be, uint32_t slot_off,
                             uint32_t *len);

/**
 * @brief Re-derive the payload CRC and compare it with the header's.
 *
 * @param be       backend to read through
 * @param slot_off byte offset of the slot
 * @return TIKU_BIGBLOB_OK, or a negative tiku_bigblob_err_t
 */
int tiku_bigblob_verify(tiku_nvm_backend_t *be, uint32_t slot_off);

#endif /* TIKU_BIGBLOB_H_ */
