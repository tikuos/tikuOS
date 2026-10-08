/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbd_msc.c - Bulk-Only Transport + SCSI, with no controller in it.
 *
 * Decodes command wrappers and SCSI commands, and builds the replies and the
 * status wrapper; the controller's transport calls in for each decision.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_usbd_msc.h"

/*---------------------------------------------------------------------------*/
/* BYTE ORDER                                                                */
/*---------------------------------------------------------------------------*/

/* SCSI is big-endian and the BOT wrappers little-endian; mixing them up is
 * silent, since both ends still see numbers. */

/** @brief Read a little-endian 32-bit value. */
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** @brief Read a big-endian 32-bit value. */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/** @brief Write @p v as a little-endian 32-bit value. */
static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/** @brief Write @p v as a big-endian 32-bit value. */
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/*---------------------------------------------------------------------------*/
/* WRAPPERS                                                                  */
/*---------------------------------------------------------------------------*/

/*
 * A CBW is valid when it is 31 bytes long and carries the signature, the two
 * tests the specification calls for.  bCBWLUN and bCBWCBLength are masked to
 * their field widths and not checked: a valid CBW that is not meaningful is
 * answered by the transport, which stalls the pipe.
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
/* SCSI                                                                      */
/*---------------------------------------------------------------------------*/

int tiku_usbd_msc_lba_ok(const tiku_usbd_msc_t *m, uint32_t lba, uint32_t nblk)
{
    if (m == NULL)          { return 0; }
    if (nblk == 0u)         { return 1; }
    /* The host sets lba and nblk, and lba + nblk can wrap past 2^32, so lba
     * is bounded first and nblk compared with the blocks left after it. */
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

/** @brief Copy @p src into the 16-byte field at @p dst, space padded; a longer
 *         @p src is cut at 16. */
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
 * small_reply() answers from memory, so the transport sends its reply in the
 * context that decoded the command.  READ(10) and WRITE(10), the only
 * commands that touch the medium, are left to the caller.
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
        /* Reading the sense clears it, so a later REQUEST SENSE reports
         * only a later failure. */
        m->sense_key = TIKU_USBD_MSC_SENSE_NONE;
        m->sense_asc = 0u;
        len = 18u;
        break;

    case TIKU_USBD_MSC_READ_CAPACITY10:
        if (m->blocks == 0) {
            tiku_usbd_msc_fail(m, TIKU_USBD_MSC_SENSE_NOTREADY,
                               TIKU_USBD_MSC_ASC_NOT_READY);
            *status = 1u;
            return 0;
        }
        /* SCSI defines the first field as the last LBA, blocks - 1. */
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
        /* An unknown opcode fails: a pass would tell the host that the
         * command, a write among them, took effect. */
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
         * A range outside the medium fails before lba and nblk are set, so
         * a caller that ignores the action reads lba 0 and nblk 0.
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
