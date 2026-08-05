/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbd_msc.c - Bulk-Only Transport + SCSI, with no controller in it.
 *
 * Carved out of the Apollo510 driver, where this logic was proven against
 * Linux, macOS and Windows hosts.  Behaviour is unchanged by construction.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_usbd_msc.h"

/*---------------------------------------------------------------------------*/
/* Byte order.  SCSI is big-endian, the BOT wrappers are little-endian, and   */
/* mixing them up is silent -- both ends still see numbers.                   */
/*---------------------------------------------------------------------------*/

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/*---------------------------------------------------------------------------*/
/* Wrappers                                                                   */
/*---------------------------------------------------------------------------*/

/*
 * Validity is exactly the two tests the specification calls for -- 31 bytes
 * and the right signature -- and deliberately no more.  bCBWLUN and
 * bCBWCBLength are surfaced for the caller to judge rather than rejected
 * here, because a wrapper that is valid but not meaningful has a defined
 * answer (stall the pipe) that belongs to the transport, not to this file.
 */
int tiku_usbd_msc_parse_cbw(const uint8_t *buf, uint16_t n,
                            tiku_usbd_msc_cbw_t *out)
{
    if (buf == NULL || out == NULL) {
        return 0;
    }
    if (n != TIKU_USBD_MSC_CBW_LEN) {
        return 0;
    }
    if (le32(&buf[0]) != TIKU_USBD_MSC_CBW_SIG) {
        return 0;
    }

    out->tag      = le32(&buf[4]);
    out->host_len = le32(&buf[8]);
    out->dir_in   = (uint8_t)((buf[12] & 0x80u) ? 1u : 0u);
    out->lun      = (uint8_t)(buf[13] & 0x0Fu);
    out->cdb_len  = (uint8_t)(buf[14] & 0x1Fu);
    out->cdb      = &buf[15];
    return 1;
}

void tiku_usbd_msc_build_csw(uint8_t *out13, uint32_t tag, uint32_t residue,
                             uint8_t status)
{
    if (out13 == NULL) {
        return;
    }
    put_le32(&out13[0], TIKU_USBD_MSC_CSW_SIG);
    put_le32(&out13[4], tag);
    put_le32(&out13[8], residue);
    out13[12] = status;
}

/*---------------------------------------------------------------------------*/
/* SCSI                                                                       */
/*---------------------------------------------------------------------------*/

int tiku_usbd_msc_lba_ok(const tiku_usbd_msc_t *m, uint32_t lba, uint32_t nblk)
{
    if (m == NULL)          { return 0; }
    if (nblk == 0u)         { return 1; }
    if (lba >= m->blocks)   { return 0; }
    return (nblk <= (m->blocks - lba)) ? 1 : 0;
}

void tiku_usbd_msc_fail(tiku_usbd_msc_t *m, uint8_t key, uint8_t asc)
{
    if (m != NULL) {
        m->sense_key = key;
        m->sense_asc = asc;
    }
}

/** @brief Copy @p src into 16 bytes at @p dst, space padded, never truncated
 *         silently past the field. */
static void pad16(uint8_t *dst, const char *src)
{
    unsigned i = 0u;

    if (src != NULL) {
        while (i < TIKU_USBD_MSC_PRODUCT_LEN && src[i] != '\0') {
            dst[i] = (uint8_t)src[i];
            i++;
        }
    }
    while (i < TIKU_USBD_MSC_PRODUCT_LEN) {
        dst[i] = (uint8_t)' ';
        i++;
    }
}

/*
 * Every command here answers out of memory, so the transport can ship the
 * reply without leaving the context it decoded in.  READ and WRITE are the
 * only opcodes that touch the medium and they are handled by the caller.
 */

/**
 * @brief Build the reply for a small, memory-resident command.
 *
 * @return bytes placed in @p r; 0 when the command carries no data phase.
 *         @p status becomes 1 on an unsupported opcode.
 */
static uint16_t small_reply(tiku_usbd_msc_t *m, const uint8_t *cb,
                            uint32_t host_len, uint8_t *r, uint8_t *status)
{
    unsigned i;
    uint16_t len = 0u;

    switch (cb[0]) {
    case TIKU_USBD_MSC_TEST_UNIT_READY:
    case TIKU_USBD_MSC_START_STOP:
    case TIKU_USBD_MSC_PREVENT_ALLOW:
    case TIKU_USBD_MSC_SYNC_CACHE10:
        return 0u;

    case TIKU_USBD_MSC_INQUIRY: {
        static const char vid[8] = { 'T','i','k','u','O','S',' ',' ' };
        for (i = 0u; i < 36u; i++) { r[i] = 0u; }
        r[0] = 0x00u;   /* direct-access block device                       */
        r[1] = 0x80u;   /* RMB: removable -- what makes a host offer to mount */
        r[2] = 0x04u;   /* SPC-2                                            */
        r[3] = 0x02u;   /* response format 2                                */
        r[4] = 31u;     /* additional length: 36 total                      */
        for (i = 0u; i < 8u; i++) { r[8 + i] = (uint8_t)vid[i]; }
        pad16(&r[16], m->product);
        r[32] = '0'; r[33] = '.'; r[34] = '0'; r[35] = '6';
        len = 36u;
        break;
    }

    case TIKU_USBD_MSC_REQUEST_SENSE:
        for (i = 0u; i < 18u; i++) { r[i] = 0u; }
        r[0]  = 0x70u;            /* current errors, fixed format           */
        r[2]  = m->sense_key;
        r[7]  = 10u;              /* additional sense length                */
        r[12] = m->sense_asc;
        /* Reading the sense CLEARS it: the condition belongs to the command
         * that caused it, and leaving it latched fails the next one too. */
        m->sense_key = TIKU_USBD_MSC_SENSE_NONE;
        m->sense_asc = 0u;
        len = 18u;
        break;

    case TIKU_USBD_MSC_READ_CAPACITY10:
        /* LAST addressable LBA, not the block count.  Reporting the count
         * gives the host one block more than exists, and it finds out by
         * reading past the end -- usually while writing a filesystem. */
        put_be32(&r[0], m->blocks - 1u);
        put_be32(&r[4], TIKU_USBD_MSC_BLOCK);
        len = 8u;
        break;

    case TIKU_USBD_MSC_MODE_SENSE6:
        r[0] = 3u; r[1] = 0u; r[2] = 0u; r[3] = 0u;
        len = 4u;
        break;

    case TIKU_USBD_MSC_MODE_SENSE10:
        for (i = 0u; i < 8u; i++) { r[i] = 0u; }
        r[1] = 6u;
        len = 8u;
        break;

    default:
        /* Refused, not ignored.  A command silently treated as success is
         * how a host comes to believe a write landed. */
        tiku_usbd_msc_fail(m, TIKU_USBD_MSC_SENSE_ILLEGAL,
                           TIKU_USBD_MSC_ASC_OPCODE);
        *status = 1u;
        return 0u;
    }

    if ((uint32_t)len > host_len) { len = (uint16_t)host_len; }
    return len;
}

void tiku_usbd_msc_decode(tiku_usbd_msc_t *m, const tiku_usbd_msc_cbw_t *cbw,
                          uint8_t *reply, tiku_usbd_msc_cmd_t *out)
{
    const uint8_t *cb;
    uint32_t host_len, bytes;
    uint8_t op;

    if (m == NULL || cbw == NULL || reply == NULL || out == NULL) {
        return;
    }
    cb       = cbw->cdb;
    host_len = cbw->host_len;
    op       = cb[0];

    out->action  = TIKU_USBD_MSC_ACT_NONE;
    out->lba     = 0u;
    out->nblk    = 0u;
    out->bytes   = 0u;
    out->residue = host_len;
    out->len     = 0u;
    out->status  = 0u;

    if (op == TIKU_USBD_MSC_READ10 || op == TIKU_USBD_MSC_WRITE10) {
        uint32_t lba  = be32(&cb[2]);
        uint32_t nblk = (uint32_t)cb[7] << 8 | cb[8];

        bytes = nblk * TIKU_USBD_MSC_BLOCK;

        /*
         * Range check BEFORE the caller is handed an LBA -- and on refusal
         * the range is NOT published either.  A caller that forgets to test
         * the action then indexes block zero rather than off the end of the
         * medium, which is the difference between a wrong answer and a
         * memory fault.
         */
        if (!tiku_usbd_msc_lba_ok(m, lba, nblk)) {
            tiku_usbd_msc_fail(m, TIKU_USBD_MSC_SENSE_ILLEGAL,
                               TIKU_USBD_MSC_ASC_LBA_RANGE);
            out->status  = 1u;
            out->residue = host_len;
            return;
        }

        if (bytes > host_len) { bytes = host_len; }
        out->lba     = lba;
        out->nblk    = nblk;
        out->bytes   = bytes;
        out->residue = host_len - bytes;
        out->action  = (op == TIKU_USBD_MSC_READ10)
                       ? TIKU_USBD_MSC_ACT_READ : TIKU_USBD_MSC_ACT_WRITE;
        return;
    }

    out->len = small_reply(m, cb, host_len, reply, &out->status);
    if (out->len != 0u) {
        out->action  = TIKU_USBD_MSC_ACT_REPLY;
        out->residue = host_len - out->len;
    } else {
        out->residue = host_len;
    }
}
