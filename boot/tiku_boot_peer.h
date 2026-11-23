/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot_peer.h - the peer of a board with one, reached over its link UART.
 *
 * The board header names the peer (TIKU_BOARD_PEER_NAME).  Its services are
 * listed here once, for the peer table and for the endpoint table's rows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOOT_PEER_H_
#define TIKU_BOOT_PEER_H_

#include "tiku.h"
#include <services/peer/tiku_peer_wire.h>

#if (TIKU_PEER_ENABLE + 0)

/**
 * @brief The peer's services as X(name, channel): ctl for the pair's own
 *        control messages, msg for applications.  The channel is also the
 *        service's index.
 */
#define TIKU_BOOT_PEER_SERVICES(X)  X("ctl", 0) X("msg", 1)

/** @brief The peer's driver state; its wire sessions back the rows below. */
extern tiku_peer_wire_t tiku_boot_peer_wire;

#define TIKU_BOOT_PEER_ROW(name, channel)                                      \
    {TIKU_BOARD_PEER_NAME "/" name, &tiku_ipc_wire_ops,                        \
     &tiku_boot_peer_wire.wire[channel], 0},

/** @brief The endpoint table's rows for the peer, consecutive and in order. */
#define TIKU_BOOT_PEER_ROWS  TIKU_BOOT_PEER_SERVICES(TIKU_BOOT_PEER_ROW)

#endif /* TIKU_PEER_ENABLE */
#endif /* TIKU_BOOT_PEER_H_ */
