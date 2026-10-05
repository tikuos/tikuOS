/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_store_arch.h - the model store: staged over USB, kept in flash.
 *
 * The host writes a model to the staging disk and then a commit record; the
 * board publishes the model to octal flash, and a restore copies it back.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_STORE_ARCH_H_
#define TIKU_RA8P1_STORE_ARCH_H_

#include <stdint.h>

/*
 * The host commits a model by writing a tiku_store_commit_t to the last
 * block of the staging disk, tiku_ra8p1_store_commit_lba().  Partition
 * probes, filesystems and dd write block 0, not the last block, so they do
 * not start an import.  The record carries the payload length, counted from
 * LBA 0, because the disk also holds whatever an earlier model left there.
 */

/** @brief Magic in the commit record: "TKIM", little-endian. */
#define TIKU_STORE_MAGIC     0x4D494B54UL

/** @brief Longest model name, excluding the terminator. */
#define TIKU_STORE_NAME_MAX  23u

/** @brief What the host writes to the sentinel block to commit. */
typedef struct {
    uint32_t magic;                          /**< TIKU_STORE_MAGIC        */
    uint32_t len;                            /**< payload bytes from LBA 0 */
    char     name[TIKU_STORE_NAME_MAX + 1u];  /**< model name              */
} tiku_store_commit_t;

/** @brief Outcome of an import attempt. */
typedef enum {
    TIKU_STORE_IDLE = 0,   /**< no commit record seen                     */
    TIKU_STORE_DONE,       /**< published and verified                    */
    TIKU_STORE_ERR_MAGIC,  /**< the sentinel block held something else    */
    TIKU_STORE_ERR_LEN,    /**< length is 0 or reaches the sentinel block */
    TIKU_STORE_ERR_WRITE,  /**< the flash refused it                      */
    TIKU_STORE_ERR_VERIFY, /**< it read back differently than it went in  */
    TIKU_STORE_BUSY,       /**< an import is running                      */
} tiku_store_state_t;

/**
 * @brief Begin an import if the host has just written a commit record.
 *
 * @param lba    first block of the write that just completed
 * @param blocks its length in blocks; unused
 * @return TIKU_STORE_BUSY once started or while an import runs,
 *         TIKU_STORE_IDLE for any other block, or a TIKU_STORE_ERR_* state
 */
tiku_store_state_t tiku_ra8p1_store_begin(uint32_t lba, uint32_t blocks);

/**
 * @brief Advance an import by one step; the last step verifies the flash.
 *
 * @param done receives payload bytes published so far; may be NULL
 * @return 1 while more remains, 0 when idle or finished
 * @note Call from the pump that serves the mass-storage transport.
 */
int tiku_ra8p1_store_step(uint32_t *done);

/** @brief Non-zero while an import owns the staging window. */
int tiku_ra8p1_store_busy(void);

/** @brief The outcome of the last import. */
tiku_store_state_t tiku_ra8p1_store_last(void);

/**
 * @brief Publish and verify a commit record's model before returning.
 *
 * Runs the whole import in one call, outside the state that
 * tiku_ra8p1_store_busy() and tiku_ra8p1_store_last() report.
 *
 * @param lba    first block of the write that just completed
 * @param blocks its length in blocks; unused
 * @return what happened; TIKU_STORE_IDLE when this was an ordinary write
 */
tiku_store_state_t tiku_ra8p1_store_on_write(uint32_t lba, uint32_t blocks);

/**
 * @brief Copy the stored model back into the staging window.
 *
 * @param out_ms receives how long it took, in milliseconds; may be NULL
 * @param out_len receives the payload length; may be NULL
 * @param name   receives the model name; may be NULL
 * @return 1 when a model was restored; 0 when the SDRAM is down, the flash
 *         is absent or the slot holds nothing
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
