/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbd_msc.h - Bulk-Only Transport + SCSI, with no controller in it.
 *
 * The wire format a mass-storage device must speak, decoupled from whatever
 * moves the bytes.  Pure functions over buffers plus one small context.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_USBD_MSC_H_
#define TIKU_USBD_MSC_H_

#include <stdint.h>
#include <stddef.h>

/*
 * What is here and what is deliberately not.
 *
 * Here: the parts every mass-storage device gets wrong in the same ways --
 * the CBW/CSW field layout, the SCSI replies a host needs to mount a volume,
 * the sense latch, and the range check that has to survive a host naming an
 * LBA near 2^32.  None of it touches a register, so it can be exercised on a
 * build machine against known-good byte sequences instead of only on a board.
 *
 * Not here: the transport.  Whether packets arrive by interrupt from a MUSB
 * FIFO or by polling a Renesas pipe is the controller's business, and the two
 * shapes differ enough that sharing the pump would mean sharing control flow
 * -- the risky half.  Each controller drives its own state machine and calls
 * in here for every decision about what the bytes mean.
 */

/** @brief Bulk-Only Transport wrapper sizes and signatures. */
#define TIKU_USBD_MSC_CBW_LEN   31u
#define TIKU_USBD_MSC_CSW_LEN   13u
#define TIKU_USBD_MSC_CBW_SIG   0x43425355u   /* "USBC", little-endian */
#define TIKU_USBD_MSC_CSW_SIG   0x53425355u   /* "USBS", little-endian */

/** @brief The only block size this speaks; hosts assume it for removable media. */
#define TIKU_USBD_MSC_BLOCK     512u

/** @brief Buffer a caller must provide for replies.  INQUIRY (36) is longest. */
#define TIKU_USBD_MSC_REPLY_MAX 64u

/** @brief Characters of INQUIRY product identification, space padded. */
#define TIKU_USBD_MSC_PRODUCT_LEN 16u

/** @brief SCSI opcodes this decodes.  Anything else is refused, not ignored. */
#define TIKU_USBD_MSC_TEST_UNIT_READY  0x00u
#define TIKU_USBD_MSC_REQUEST_SENSE    0x03u
#define TIKU_USBD_MSC_INQUIRY          0x12u
#define TIKU_USBD_MSC_MODE_SENSE6      0x1Au
#define TIKU_USBD_MSC_START_STOP       0x1Bu
#define TIKU_USBD_MSC_PREVENT_ALLOW    0x1Eu
#define TIKU_USBD_MSC_READ_CAPACITY10  0x25u
#define TIKU_USBD_MSC_READ10           0x28u
#define TIKU_USBD_MSC_WRITE10          0x2Au
#define TIKU_USBD_MSC_SYNC_CACHE10     0x35u
#define TIKU_USBD_MSC_MODE_SENSE10     0x5Au

/** @brief Sense keys and additional sense codes this raises. */
#define TIKU_USBD_MSC_SENSE_NONE       0x00u
#define TIKU_USBD_MSC_SENSE_NOTREADY   0x02u
#define TIKU_USBD_MSC_SENSE_HARDWARE   0x04u
#define TIKU_USBD_MSC_SENSE_ILLEGAL    0x05u
#define TIKU_USBD_MSC_ASC_OPCODE       0x20u   /* invalid command operation */
#define TIKU_USBD_MSC_ASC_LBA_RANGE    0x21u   /* LBA out of range          */
#define TIKU_USBD_MSC_ASC_NOT_READY    0x04u   /* becoming ready            */

/** @brief A command wrapper, decoded.  @c cdb points into the caller's buffer. */
typedef struct {
    uint32_t       tag;       /**< dCBWTag, echoed back in the status wrapper */
    uint32_t       host_len;  /**< dCBWDataTransferLength: what the host expects */
    uint8_t        dir_in;    /**< bmCBWFlags bit 7: 1 = device to host       */
    uint8_t        lun;       /**< bCBWLUN                                    */
    uint8_t        cdb_len;   /**< bCBWCBLength                               */
    const uint8_t *cdb;       /**< the SCSI command block itself              */
} tiku_usbd_msc_cbw_t;

/** @brief The medium being presented, plus the sense latch REQUEST SENSE reads. */
typedef struct {
    uint32_t    blocks;     /**< capacity, in TIKU_USBD_MSC_BLOCK units      */
    const char *product;    /**< 16 chars for INQUIRY; NULL for a default    */
    uint8_t     sense_key;  /**< latched until the host asks for it          */
    uint8_t     sense_asc;
} tiku_usbd_msc_t;

/** @brief What a decoded command asks the transport to do next. */
typedef enum {
    TIKU_USBD_MSC_ACT_NONE = 0, /**< no data phase; send the status wrapper  */
    TIKU_USBD_MSC_ACT_REPLY,    /**< send @c len bytes from the reply buffer */
    TIKU_USBD_MSC_ACT_READ,     /**< send @c bytes read from @c lba          */
    TIKU_USBD_MSC_ACT_WRITE,    /**< receive @c bytes and store them at @c lba */
} tiku_usbd_msc_action_t;

/** @brief The decoded command: everything the transport needs, nothing more. */
typedef struct {
    tiku_usbd_msc_action_t action;
    uint32_t lba;      /**< first block, for READ and WRITE                  */
    uint32_t nblk;     /**< block count as the host asked                    */
    uint32_t bytes;    /**< data-phase length, already clamped to host_len   */
    uint32_t residue;  /**< dCSWDataResidue to report                        */
    uint16_t len;      /**< ACT_REPLY only: bytes placed in the reply buffer */
    uint8_t  status;   /**< dCSWStatus: 0 pass, 1 fail                       */
} tiku_usbd_msc_cmd_t;

/**
 * @brief Validate and decode a command wrapper.
 *
 * @param buf raw bytes received on the bulk OUT endpoint
 * @param n   how many arrived
 * @param out receives the decoded wrapper on success
 * @return 1 when @p buf is a well-formed CBW, 0 when it must be ignored
 */
int tiku_usbd_msc_parse_cbw(const uint8_t *buf, uint16_t n,
                            tiku_usbd_msc_cbw_t *out);

/**
 * @brief Lay out the 13-byte status wrapper that ends every command.
 *
 * @param out13   destination, at least TIKU_USBD_MSC_CSW_LEN bytes
 * @param tag     the tag from the command being answered
 * @param residue bytes the host asked for and did not get
 * @param status  0 pass, 1 fail
 */
void tiku_usbd_msc_build_csw(uint8_t *out13, uint32_t tag, uint32_t residue,
                             uint8_t status);

/*
 * The order of the tests is the whole point.  `(lba + nblk) > blocks` is
 * wrong: the host controls both values, and an LBA near 2^32 wraps the sum
 * small, so the check passes and the caller indexes off the end of the
 * medium.  Bounding lba first makes the subtraction safe.
 */

/**
 * @brief Is this block range inside the medium?
 *
 * @param m    the medium
 * @param lba  first block
 * @param nblk block count
 * @return 1 when the range is wholly inside, 0 otherwise
 */
int tiku_usbd_msc_lba_ok(const tiku_usbd_msc_t *m, uint32_t lba,
                         uint32_t nblk);

/**
 * @brief Latch a sense condition for the REQUEST SENSE that will follow.
 *
 * @param m   the medium whose latch to set
 * @param key sense key
 * @param asc additional sense code
 */
void tiku_usbd_msc_fail(tiku_usbd_msc_t *m, uint8_t key, uint8_t asc);

/**
 * @brief Decode one SCSI command and say what data phase it needs.
 *
 * @param m     the medium; its sense latch may be read and cleared
 * @param cbw   the wrapper the command arrived in
 * @param reply buffer of TIKU_USBD_MSC_REPLY_MAX bytes for small replies
 * @param out   receives the decision
 */
void tiku_usbd_msc_decode(tiku_usbd_msc_t *m, const tiku_usbd_msc_cbw_t *cbw,
                          uint8_t *reply, tiku_usbd_msc_cmd_t *out);

#endif /* TIKU_USBD_MSC_H_ */
