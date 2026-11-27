/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer_ctl.h - the pair's control service: the link's speed, agreed
 * with the peer on <peer>/ctl.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PEER_CTL_H_
#define TIKU_PEER_CTL_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* MESSAGES                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Bytes of a message: kind, op, id, result, then the speed in bits
 *         a second, 32-bit little-endian. */
#define TIKU_PEER_CTL_MSG       8u
#define TIKU_PEER_CTL_KIND      0x43u   /**< 'C' */

/** @defgroup TIKU_PEER_CTL_OPS Operations
 * @{ */
#define TIKU_PEER_CTL_ASK       1u  /**< May the link run at this speed? */
#define TIKU_PEER_CTL_ANSWER    2u  /**< The result: yes, no or busy */
#define TIKU_PEER_CTL_CHECK     3u  /**< Sent at the new speed by the asker */
#define TIKU_PEER_CTL_CHECKED   4u  /**< Its answer: the change holds */
#define TIKU_PEER_CTL_NOTICE    5u  /**< Something changed; id names what */
/** @} */

/** @defgroup TIKU_PEER_CTL_TOPICS What a notice is about: 1 to 8
 * @{ */
#define TIKU_PEER_CTL_TOPIC_MAILBOX  1u  /**< A file of the shared mailbox */
/** @} */

/** @defgroup TIKU_PEER_CTL_RESULTS Answers to an ASK
 * @{ */
#define TIKU_PEER_CTL_YES       0u
#define TIKU_PEER_CTL_NO        1u  /**< No setting for that speed */
#define TIKU_PEER_CTL_BUSY      2u  /**< A change is already under way */
/** @} */

/*---------------------------------------------------------------------------*/
/* LIMITS                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Peers of the table, from the first, the service handles: 1 or 2. */
#ifndef TIKU_PEER_CTL_PEERS
#define TIKU_PEER_CTL_PEERS     1u
#endif
#if TIKU_PEER_CTL_PEERS < 1 || TIKU_PEER_CTL_PEERS > 2
#error "TIKU_PEER_CTL_PEERS is 1 or 2"
#endif

/** @brief Seconds this side waits for an answer, and the asker for CHECKED
 *         after its switch; the answering side waits one more for CHECK. */
#ifndef TIKU_PEER_CTL_WAIT_S
#define TIKU_PEER_CTL_WAIT_S    3u
#endif

/** @brief Seconds between tries toward the speed asked for, and the tries
 *         after which a speed the peer does not take is given up. */
#define TIKU_PEER_CTL_AGAIN_S   5u
#define TIKU_PEER_CTL_TRIES     3u

/*---------------------------------------------------------------------------*/
/* FUNCTIONS                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Tell every peer the service handles that @p topic (1 to 8) changed.
 *        The notice goes out when the link takes it; notices on one topic
 *        that are not yet out become one.
 */
void tiku_peer_ctl_notice(uint8_t topic);

/** @brief Call @p fn, in the service's process, with each notice a peer
 *         sends; NULL removes it. */
void tiku_peer_ctl_on_notice(void (*fn)(uint8_t index, uint8_t topic));

/** @brief Failed changes of speed with the peer at @p index since boot. */
uint16_t tiku_peer_ctl_failures(uint8_t index);

/** @brief Start the process that owns every peer's "ctl" service, once. */
void tiku_peer_ctl_start(void);

#endif /* TIKU_PEER_CTL_H_ */
