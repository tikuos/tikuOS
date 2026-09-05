/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_console.h - a link on one marked channel of the console.
 *
 * The console has no flow control and loses bytes while the board is busy,
 * so this backend carries a CRC-16 on every frame and answers a peer's
 * numbered stream with acknowledgements.  The console confers full authority.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_LINK_CONSOLE_H_
#define TIKU_LINK_CONSOLE_H_

#include "tiku_link.h"

/** @brief The channel marker a desktop's window session rides. */
#define TIKU_LINK_CONSOLE_SESSION 0xF1u

/** @brief The largest message the window session's channel takes. */
#define TIKU_LINK_CONSOLE_SESSION_MTU 256u

/*
 * The frame on the channel, after the marker the console strips:
 *
 *   +0  ctl      bits 0-1 the kind, bit 7 WANT_ACK, the other bits zero
 *   +1  seq      DATA and SYN: this frame's number; ACK: the last delivered
 *   +2  payload  DATA only
 *   -2  crc      CRC-16/CCITT-FALSE over ctl..payload, high byte first
 *
 * A DATA sent with WANT_ACK is delivered once and in order, and answered
 * with an ACK carrying its seq; a repeat of one already delivered is
 * answered again and not delivered; one out of order is dropped
 * unanswered.  A DATA without WANT_ACK is delivered as it comes.  A SYN
 * starts the paced stream at its seq, restarts the receiver's own
 * numbering at zero, and is answered like a DATA.  A frame whose CRC
 * disagrees, or shorter than a header and a CRC, is dropped whole.
 */
#define TIKU_LINK_CONSOLE_OVERHEAD  4u     /**< header + CRC around a message */
#define TIKU_LINK_CONSOLE_DATA      0x00u
#define TIKU_LINK_CONSOLE_ACK       0x01u
#define TIKU_LINK_CONSOLE_SYN       0x02u
#define TIKU_LINK_CONSOLE_KIND      0x03u  /**< the kind's bits of ctl */
#define TIKU_LINK_CONSOLE_WANT_ACK  0x80u

/**
 * @brief How far behind the expected seq a paced frame still counts as a
 *        repeat of one delivered (its ACK was lost) rather than a stranger.
 */
#define TIKU_LINK_CONSOLE_DUP_SPAN  8u

/** @brief The console link's state: the caller keeps it, statically. */
typedef struct {
    tiku_link_t link;
    uint8_t     marker;
    uint8_t     tx_seq;        /**< the number of the next frame sent */
    uint8_t     paced_expect;  /**< the next WANT_ACK seq to deliver */
    uint8_t     paced_synced;  /**< paced_expect is meaningful */
    uint8_t     plain_expect;  /**< the next seq of the unpaced stream */
    uint8_t     plain_seen;    /**< plain_expect is meaningful */
} tiku_link_console_t;

/** @brief Counters over every console link since boot. */
typedef struct {
    uint32_t rx;      /**< messages delivered */
    uint32_t tx;      /**< DATA frames sent */
    uint32_t bad;     /**< frames dropped: too short, or the CRC disagrees */
    uint32_t dup;     /**< paced frames seen again: answered, not delivered */
    uint32_t reject;  /**< paced frames out of order: dropped, unanswered */
    uint32_t acked;   /**< ACKs sent */
    uint32_t gap;     /**< unpaced frames missing before one that arrived */
} tiku_link_console_stats_t;

/**
 * @brief Open @p lc as a link on the console channel @p marker, receiving
 *        into @p buf of @p cap bytes.
 *
 * The link sends its own frames unpaced.  A peer's paced frames are
 * acknowledged from inside the console's pump.
 *
 * @note @p buf must hold the largest message plus TIKU_LINK_CONSOLE_OVERHEAD.
 * @return the link, or NULL when the console has no channel slot left
 */
tiku_link_t *tiku_link_console_open(tiku_link_console_t *lc, uint8_t marker,
                                    uint8_t *buf, size_t cap);

/** @brief The counters since boot, or since the last reset. */
const tiku_link_console_stats_t *tiku_link_console_stats(void);

/** @brief Zero the counters. */
void tiku_link_console_stats_reset(void);

/** @brief CRC-16/CCITT-FALSE continued from @p crc; start it at 0xFFFF. */
uint16_t tiku_link_console_crc(uint16_t crc, const void *bytes, size_t len);

#endif /* TIKU_LINK_CONSOLE_H_ */
