/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_store_arch.h - the model store: staged over USB, kept in flash.
 *
 * Host writes a model to the staging disk and then a commit record; the
 * board publishes it to flash and restores it at every boot afterwards.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_STORE_ARCH_H_
#define TIKU_RA8P1_STORE_ARCH_H_

#include <stdint.h>

/*
 * WHY A COMMIT RECORD RATHER THAN A COMMAND.
 *
 * The board has no shell in this configuration and the host has no channel
 * to it except the disk itself, so the disk has to carry the instruction.
 * A sentinel BLOCK is the least surprising way to do that: writing the last
 * block of a raw device is not something any tool does incidentally, so the
 * trigger cannot be pulled by a partition probe, a filesystem, or a stray
 * dd -- whereas block 0 is exactly where all three write.
 *
 * The record names the payload's length rather than the board inferring it,
 * because a staging disk holds whatever the previous occupant left behind
 * and "everything up to the last byte anyone wrote" is not a length.
 */

/** @brief Magic in the commit record: "TKIM", little-endian. */
#define TIKU_STORE_MAGIC     0x4D494B54UL

/** @brief Longest model name, excluding the terminator. */
#define TIKU_STORE_NAME_MAX  23u

/** @brief What the host writes to the sentinel block to commit. */
typedef struct {
    uint32_t magic;                          /**< TIKU_STORE_MAGIC        */
    uint32_t len;                            /**< payload bytes from LBA 0 */
    char     name[TIKU_STORE_NAME_MAX + 1u];
} tiku_store_commit_t;

/** @brief Outcome of an import attempt. */
typedef enum {
    TIKU_STORE_IDLE = 0,   /**< no commit record seen                     */
    TIKU_STORE_DONE,       /**< published and verified                    */
    TIKU_STORE_ERR_MAGIC,  /**< the sentinel block held something else    */
    TIKU_STORE_ERR_LEN,    /**< the length does not fit the staging disk  */
    TIKU_STORE_ERR_WRITE,  /**< the flash refused it                      */
    TIKU_STORE_ERR_VERIFY, /**< it read back differently than it went in  */
} tiku_store_state_t;

/**
 * @brief Act on a commit record if the host has just written one.
 *
 * @param lba    first block of the write that just completed
 * @param blocks its length in blocks
 * @return what happened; TIKU_STORE_IDLE when this was an ordinary write
 */
tiku_store_state_t tiku_ra8p1_store_on_write(uint32_t lba, uint32_t blocks);

/**
 * @brief Copy the stored model back into the staging window.
 *
 * @param out_ms receives how long it took, in milliseconds; may be NULL
 * @param out_len receives the payload length; may be NULL
 * @param name   receives the model name; may be NULL
 * @return 1 when a model was restored, 0 when the slot holds nothing
 */
int tiku_ra8p1_store_restore(uint32_t *out_ms, uint32_t *out_len, char *name);

/**
 * @brief Name and length of the stored model, without reading its payload.
 *
 * @param name receives the name, TIKU_STORE_NAME_MAX + 1 bytes; may be NULL
 * @param len  receives the payload length; may be NULL
 * @return 1 when a model is stored, 0 when the slot is empty
 */
int tiku_ra8p1_store_info(char *name, uint32_t *len);

/**
 * @brief Re-derive the stored model's CRC and compare it with its header.
 *
 * @return 1 when the payload still matches, 0 otherwise
 */
int tiku_ra8p1_store_verify(void);

/** @brief The sentinel block a commit record must be written to. */
uint32_t tiku_ra8p1_store_commit_lba(void);

#endif /* TIKU_RA8P1_STORE_ARCH_H_ */
