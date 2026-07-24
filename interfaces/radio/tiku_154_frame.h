/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_154_frame.h - IEEE 802.15.4-2006 MAC frame primitives (PHY-free).
 *
 * The N-track's groundwork (kintsugi/radio.md), the 802.15.4 analog of the
 * L-track's CSA#1 / SN-NESN helpers: the fiddly, bug-prone, PHY-INDEPENDENT
 * logic -- FCS, channel<->frequency, and the MHR build/parse with its
 * addressing-mode + PAN-ID-compression rules -- done and verified before the
 * PHY bring-up (MODE=0xF, SFD, ED/CCA) needs it.  Deliberately register-free
 * so it host-compiles and self-tests off-target; the nordic RADIO computes
 * the FCS in hardware, but a software copy is what a sniffer/validator, an
 * ACK builder, and any FLPR/host path all need.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_154_FRAME_H_
#define TIKU_154_FRAME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Frame types (FCF bits 0-2). */
#define TIKU_154_FT_BEACON   0u
#define TIKU_154_FT_DATA     1u
#define TIKU_154_FT_ACK      2u
#define TIKU_154_FT_CMD      3u

/* Addressing modes (FCF bits 10-11 dest, 14-15 src). */
#define TIKU_154_ADDR_NONE   0u
#define TIKU_154_ADDR_SHORT  2u   /* 16-bit */
#define TIKU_154_ADDR_EXT    3u   /* 64-bit */

/* 2.4 GHz O-QPSK channels. */
#define TIKU_154_CHAN_MIN    11u
#define TIKU_154_CHAN_MAX    26u

/**
 * @brief IEEE 802.15.4 FCS: CRC-16/KERMIT (poly x^16+x^12+x^5+1, reflected
 *        0x8408, init 0, no final xor).  Check value FCS("123456789")=0x2189.
 */
uint16_t tiku_154_fcs(const uint8_t *data, uint16_t len);

/** @brief Channel (11..26) -> centre frequency in MHz, 0 if out of range. */
uint16_t tiku_154_chan_freq(uint8_t chan);

/** @brief Channel -> RADIO FREQUENCY offset (freq - 2400), 0xFFFF if bad. */
uint16_t tiku_154_chan_offset(uint8_t chan);

/** Parsed / to-build MAC header.  Addresses are little-endian (on-air). */
typedef struct {
    uint8_t  type;          /**< TIKU_154_FT_*                            */
    uint8_t  ack_req;       /**< AR bit                                   */
    uint8_t  pan_compress;  /**< PAN ID compression bit                   */
    uint8_t  seq;           /**< sequence number                          */
    uint8_t  dst_mode;      /**< TIKU_154_ADDR_*                          */
    uint16_t dst_pan;
    uint8_t  dst_addr[8];   /**< short uses [0..1]                        */
    uint8_t  src_mode;
    uint16_t src_pan;       /**< inherits dst_pan when compressed         */
    uint8_t  src_addr[8];
} tiku_154_mhr_t;

/**
 * @brief Serialise a MAC header into @p buf.
 * @return Header length in bytes (the payload offset); 0 on a reserved
 *         addressing mode.  @p buf must hold >= 23 bytes (worst case:
 *         FCF+seq + dstPAN+ext + srcPAN+ext).
 */
uint16_t tiku_154_mhr_build(uint8_t *buf, const tiku_154_mhr_t *h);

/**
 * @brief Parse a MAC header from @p buf (@p len bytes available).
 * @return Header length (offset of the MAC payload), or 0 if the frame is
 *         truncated or uses a reserved addressing mode.  With PAN
 *         compression, @p h->src_pan is set to the dest PAN.
 */
uint16_t tiku_154_mhr_parse(const uint8_t *buf, uint16_t len,
                            tiku_154_mhr_t *h);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_154_FRAME_H_ */
