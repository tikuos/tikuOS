/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer_wire.h - a peer driver for a carrier of whole messages.
 *
 * One physical link, a channel per service, and on each channel the IPC wire
 * session, whose hello tells this side when the peer is there and when it
 * has come back as a new instance.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PEER_WIRE_H_
#define TIKU_PEER_WIRE_H_

#include <interfaces/peer/tiku_peer.h>
#include <services/ipc/tiku_ipc_mux.h>
#include <services/ipc/tiku_ipc_wire.h>
#include <kernel/timers/tiku_clock.h>

#if (TIKU_PEER_ENABLE + 0)

/** @brief Most services one wire peer carries. */
#ifndef TIKU_PEER_WIRE_SERVICES
#define TIKU_PEER_WIRE_SERVICES  2u
#endif

/**
 * @brief What the board supplies for one wire peer.
 *
 * link is called once, at bind, and its link stays open.  carrier opens or
 * parks the medium under it around a start and a stop; NULL means it is up
 * from boot.  power, reset and powered are NULL on the worker side.
 */
typedef struct {
    tiku_link_t *(*link)(void);
    int          (*carrier)(uint8_t up);
    int          (*power)(uint8_t on);
    int          (*reset)(void);
    uint8_t      (*powered)(void);
} tiku_peer_wire_board_t;

/** @brief One wire peer: what it is, then its state. */
typedef struct {
    const tiku_peer_t            *peer;
    const tiku_peer_wire_board_t *board;
    uint16_t                      frame_size;   /**< wire frame, 80 to 256 */
    tiku_clock_time_t             start_ticks;  /**< unanswered this long: LOST */

    tiku_ipc_mux_t                mux;
    tiku_ipc_wire_t               wire[TIKU_PEER_WIRE_SERVICES];
    uint64_t                      seen_epoch;   /**< the peer instance last seen */
    tiku_clock_time_t             waiting_since;
    uint16_t                      first_endpoint;
    uint16_t                      restarts;
    uint8_t                       bound;
    uint8_t                       carrier_up;
} tiku_peer_wire_t;

/** @brief The driver; a peer's context is its tiku_peer_wire_t. */
extern const tiku_peer_ops_t tiku_peer_wire_ops;

/**
 * @brief The board's link UART as a whole-message link, opened once.
 *
 * Reads and writes nothing while the UART is parked, so the link stays open
 * across a stop and the next start.
 */
tiku_link_t *tiku_peer_uart_link(void);

/** @brief Open (1) or park (0) the board's link UART.  @return 0 or -1 */
int tiku_peer_uart_carrier(uint8_t up);

#endif /* TIKU_PEER_ENABLE */
#endif /* TIKU_PEER_WIRE_H_ */
