/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * ref_original.c - reference copy of the Apollo510 mass-storage SCSI logic.
 *
 * A frozen copy of the SCSI decisions and CSW layout of the Apollo510 USB
 * driver, with a ref_ prefix: the reference msc_diff_test compares
 * kernel/usb/tiku_usbd_msc.c against.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The logic here stays as copied.  msc_diff_test fails on any input where
 * tiku_usbd_msc.c answers differently, so an intended behaviour change to
 * tiku_usbd_msc.c is made here as well, in the same commit.
 */

#include "ref_original.h"

/* --- module state, exported so msc_diff_test can set and compare it ----- */
uint32_t ref_msc_blocks = 2048u;
uint8_t  ref_sense_key, ref_sense_asc;
uint8_t  ref_bot_status;
uint8_t  ref_reply[64];
int      ref_store_is_emmc;

#define MSC_BLOCK_SIZE   512u

#define SCSI_TEST_UNIT_READY   0x00u
#define SCSI_REQUEST_SENSE     0x03u
#define SCSI_INQUIRY           0x12u
#define SCSI_MODE_SENSE6       0x1Au
#define SCSI_START_STOP        0x1Bu
#define SCSI_PREVENT_ALLOW     0x1Eu
#define SCSI_READ_CAPACITY10   0x25u
#define SCSI_READ10            0x28u
#define SCSI_WRITE10           0x2Au
#define SCSI_MODE_SENSE10      0x5Au
#define SCSI_SYNC_CACHE10      0x35u

/* --- verbatim ----------------------------------------------------------- */

/** @brief Read a big-endian 32-bit field. */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
/** @brief Write a big-endian 32-bit field. */
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

int ref_msc_lba_ok(uint32_t lba, uint32_t nblk)
{
    if (nblk == 0u)           { return 1; }
    if (lba >= ref_msc_blocks)  { return 0; }
    return (nblk <= (ref_msc_blocks - lba)) ? 1 : 0;
}

/** @brief Latch sense @p key and @p asc and fail the command. */
static void msc_fail(uint8_t key, uint8_t asc)
{
    ref_sense_key = key;
    ref_sense_asc = asc;
    ref_bot_status = 1u;
}

uint16_t ref_msc_small_reply(const uint8_t *cb, uint32_t host_len)
{
    uint8_t *r = ref_reply;
    unsigned i;
    uint16_t len = 0u;

    switch (cb[0]) {
    case SCSI_TEST_UNIT_READY:
    case SCSI_START_STOP:
    case SCSI_PREVENT_ALLOW:
    case SCSI_SYNC_CACHE10:
        return 0u;

    case SCSI_INQUIRY: {
        static const char vid[8]  = { 'T','i','k','u','O','S',' ',' ' };
        for (i = 0u; i < 36u; i++) { r[i] = 0u; }
        r[0] = 0x00; r[1] = 0x80; r[2] = 0x04; r[3] = 0x02; r[4] = 31;
        for (i = 0u; i < 8u; i++) { r[8 + i] = (uint8_t)vid[i]; }
        if (ref_store_is_emmc) {
            static const char pe[16] = { 'e','M','M','C',' ','8','G','B',
                                         ' ',' ',' ',' ',' ',' ',' ',' ' };
            for (i = 0u; i < 16u; i++) { r[16 + i] = (uint8_t)pe[i]; }
        } else {
            static const char pr[16] = { 'R','A','M',' ','D','i','s','k',
                                         ' ',' ',' ',' ',' ',' ',' ',' ' };
            for (i = 0u; i < 16u; i++) { r[16 + i] = (uint8_t)pr[i]; }
        }
        r[32] = '0'; r[33] = '.'; r[34] = '0'; r[35] = '6';
        len = 36u;
        break;
    }

    case SCSI_REQUEST_SENSE:
        for (i = 0u; i < 18u; i++) { r[i] = 0u; }
        r[0] = 0x70; r[2] = ref_sense_key; r[7] = 10; r[12] = ref_sense_asc;
        ref_sense_key = 0u; ref_sense_asc = 0u;
        len = 18u;
        break;

    case SCSI_READ_CAPACITY10:
        put_be32(&r[0], ref_msc_blocks - 1u);   /* last LBA, not the count   */
        put_be32(&r[4], MSC_BLOCK_SIZE);
        len = 8u;
        break;

    case SCSI_MODE_SENSE6:
        r[0] = 3; r[1] = 0; r[2] = 0; r[3] = 0;
        len = 4u;
        break;

    case SCSI_MODE_SENSE10:
        for (i = 0u; i < 8u; i++) { r[i] = 0u; }
        r[1] = 6;
        len = 8u;
        break;

    default:
        msc_fail(0x05u, 0x20u);               /* ILLEGAL REQUEST / opcode  */
        return 0u;
    }
    if (len > host_len) { len = (uint16_t)host_len; }
    return len;
}

/*
 * The decision half of the driver's msc_scsi(): each FIFO call is replaced by
 * a record in *out of what it was asked to do.  Control flow, assignment
 * order and constants are as copied.
 */
void ref_msc_scsi(const uint8_t *cb, uint32_t host_len, ref_cmd_t *out)
{
    uint8_t op = cb[0];
    uint32_t lba, nblk, bytes;

    ref_bot_status = 0u;
    out->residue = host_len;
    out->action = REF_ACT_NONE;
    out->lba = 0u; out->nblk = 0u; out->bytes = 0u; out->len = 0u;

    switch (op) {
    case SCSI_READ10:
    case SCSI_WRITE10:
        lba  = be32(&cb[2]);
        nblk = (uint32_t)cb[7] << 8 | cb[8];
        bytes = nblk * MSC_BLOCK_SIZE;
        if (!ref_msc_lba_ok(lba, nblk)) {
            msc_fail(0x05u, 0x21u);       /* ILLEGAL REQUEST / LBA range   */
            out->residue = host_len;
            out->status = ref_bot_status;
            return;
        }
        if (bytes > host_len) { bytes = host_len; }
        out->lba = lba;
        out->nblk = nblk;
        out->bytes = bytes;
        out->residue = host_len - bytes;
        out->action = (op == SCSI_READ10) ? REF_ACT_READ : REF_ACT_WRITE;
        out->status = ref_bot_status;
        return;

    default: {
        uint16_t len = ref_msc_small_reply(cb, host_len);
        if (len) {
            out->len = len;
            out->residue = host_len - len;
            out->action = REF_ACT_REPLY;
        } else {
            out->residue = host_len;
        }
        out->status = ref_bot_status;
        return;
    }
    }
}

/** @brief Write the 13-byte CSW, byte by byte as the driver wrote it. */
void ref_build_csw(uint8_t *p, uint32_t tag, uint32_t residue, uint8_t status)
{
    p[0] = 0x55; p[1] = 0x53; p[2] = 0x42; p[3] = 0x53;   /* "USBS" LE      */
    p[4] = (uint8_t)tag;         p[5] = (uint8_t)(tag >> 8);
    p[6] = (uint8_t)(tag >> 16); p[7] = (uint8_t)(tag >> 24);
    p[8]  = (uint8_t)residue;
    p[9]  = (uint8_t)(residue >> 8);
    p[10] = (uint8_t)(residue >> 16);
    p[11] = (uint8_t)(residue >> 24);
    p[12] = status;
}
