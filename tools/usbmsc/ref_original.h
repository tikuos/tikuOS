/*
 * Tiku Operating System v0.06
 *
 * ref_original.h - interface to the pre-extraction oracle.  See the .c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef REF_ORIGINAL_H_
#define REF_ORIGINAL_H_

#include <stdint.h>

typedef enum {
    REF_ACT_NONE = 0,
    REF_ACT_REPLY,
    REF_ACT_READ,
    REF_ACT_WRITE
} ref_action_t;

typedef struct {
    ref_action_t action;
    uint32_t lba, nblk, bytes, residue;
    uint16_t len;
    uint8_t  status;
} ref_cmd_t;

extern uint32_t ref_msc_blocks;
extern uint8_t  ref_sense_key, ref_sense_asc;
extern uint8_t  ref_bot_status;
extern uint8_t  ref_reply[64];
extern int      ref_store_is_emmc;

int      ref_msc_lba_ok(uint32_t lba, uint32_t nblk);
uint16_t ref_msc_small_reply(const uint8_t *cb, uint32_t host_len);
void     ref_msc_scsi(const uint8_t *cb, uint32_t host_len, ref_cmd_t *out);
void     ref_build_csw(uint8_t *p, uint32_t tag, uint32_t residue,
                       uint8_t status);

#endif /* REF_ORIGINAL_H_ */
