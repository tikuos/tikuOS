/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * ref_original.h - interface to the reference mass-storage SCSI logic.
 *
 * Declares the ref_-prefixed copy in ref_original.c that msc_diff_test runs
 * beside kernel/usb/tiku_usbd_msc.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef REF_ORIGINAL_H_
#define REF_ORIGINAL_H_

#include <stdint.h>

/** @brief The data phase ref_msc_scsi() decides on for a command. */
typedef enum {
    REF_ACT_NONE = 0,       /**< no data phase                             */
    REF_ACT_REPLY,          /**< send len bytes of ref_reply               */
    REF_ACT_READ,           /**< read nblk blocks from lba                 */
    REF_ACT_WRITE           /**< write nblk blocks at lba                  */
} ref_action_t;

/** @brief One decoded command; bytes is the data-phase length. */
typedef struct {
    ref_action_t action;
    uint32_t lba, nblk, bytes, residue;     /**< residue: for the CSW      */
    uint16_t len;                           /**< reply length              */
    uint8_t  status;                        /**< CSW status, 0 or 1        */
} ref_cmd_t;

/** @brief Disk size in 512-byte blocks. */
extern uint32_t ref_msc_blocks;
/** @brief Sense key and ASC that REQUEST SENSE reports, then clears. */
extern uint8_t  ref_sense_key, ref_sense_asc;
/** @brief CSW status of the last command: 1 when it failed. */
extern uint8_t  ref_bot_status;
/** @brief Reply buffer for the commands without a block transfer. */
extern uint8_t  ref_reply[64];
/** @brief Non-zero: INQUIRY names an eMMC; zero: a RAM disk. */
extern int      ref_store_is_emmc;

/** @brief 1 when @p nblk blocks from @p lba fit; zero blocks always fit. */
int      ref_msc_lba_ok(uint32_t lba, uint32_t nblk);
/**
 * @brief Build, in ref_reply, the reply to a command without a transfer.
 * @return Reply length, cut to @p host_len; 0 for no data or an unknown
 *         opcode, which also sets ILLEGAL REQUEST sense and fails the command
 */
uint16_t ref_msc_small_reply(const uint8_t *cb, uint32_t host_len);
/** @brief Decode one command block into @p out, as the driver decided it. */
void     ref_msc_scsi(const uint8_t *cb, uint32_t host_len, ref_cmd_t *out);
/** @brief Write the 13-byte CSW: "USBS", tag, residue, status. */
void     ref_build_csw(uint8_t *p, uint32_t tag, uint32_t residue,
                       uint8_t status);

#endif /* REF_ORIGINAL_H_ */
