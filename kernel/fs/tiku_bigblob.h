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
 * WHY NOT TFS, AND WHY NOT tiku_blob.
 *
 * Both exist and both are the right answer for what they were built for:
 * many small files on byte-writable NVM.  Neither fits a 62 MB model on NOR
 * flash.  TFS allocates fixed-size slots and assumes a 4-byte write
 * granularity, where this medium erases in 4 KB blocks and cannot rewrite in
 * place at all; tiku_blob spans an object across at most 1000 TFS slots, so
 * the same object would need 62 KB slots and more of them than TFS has.
 *
 * The access pattern is also nothing like a filesystem's.  A model is written
 * once, read whole, and never modified -- so the structure that fits is a
 * slot, a length, a checksum, and nothing else.  Directories, free lists and
 * in-place update are all machinery for a problem this does not have.
 */

/** @brief Result codes (0 = success, negative = failure). */
typedef enum {
    TIKU_BIGBLOB_OK        =  0,
    TIKU_BIGBLOB_ERR_PARAM = -1,  /**< NULL argument or an impossible size  */
    TIKU_BIGBLOB_ERR_NOENT = -2,  /**< the slot holds no blob               */
    TIKU_BIGBLOB_ERR_SPACE = -3,  /**< the blob does not fit the medium     */
    TIKU_BIGBLOB_ERR_CRC   = -4,  /**< contents do not match the header     */
    TIKU_BIGBLOB_ERR_IO    = -5,  /**< the backend refused a write or erase */
} tiku_bigblob_err_t;

/** @brief Longest blob name, excluding the terminator. */
#define TIKU_BIGBLOB_NAME_MAX  23u

/**
 * @brief Bytes reserved for a slot's header.
 *
 * Sized to the LARGEST erase granularity the medium offers, not to the header,
 * so the payload begins on a block boundary and a blob's erase can never reach
 * into a neighbouring slot's block.  That is a containment property, not a
 * speed one: this was raised from 4 KB expecting the aligned case to erase
 * faster, and measurement said otherwise -- 1 MB took 5127 ms aligned against
 * 5084 ms unaligned, because erase time on this flash tracks the AREA cleared
 * rather than the number of commands issued.  The claim is recorded because
 * the opposite is the natural guess.
 */
#define TIKU_BIGBLOB_HDR_BYTES 65536u

/** @brief What a slot holds, as reported by tiku_bigblob_info(). */
typedef struct {
    uint32_t len;                              /**< payload bytes          */
    uint32_t crc;                              /**< CRC32 over the payload */
    char     name[TIKU_BIGBLOB_NAME_MAX + 1u];
} tiku_bigblob_info_t;

/**
 * @brief Write a blob into the slot at @p slot_off, replacing any previous.
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
