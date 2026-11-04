/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_ipc_154.h - Leased IEEE 802.15.4 transactions over a mailbox endpoint.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_154_H_
#define TIKU_IPC_154_H_
#include "tiku_ipc_wire.h"
#include <interfaces/wireless/tiku_wireless_ipc.h>
#define TIKU_154_CAP_FRAMES 1u
#define TIKU_154_CAP_ACK 2u
#define TIKU_154_CAP_ED_CCA 4u
#define TIKU_154_IPC_CAPS (TIKU_154_CAP_FRAMES | TIKU_154_CAP_ACK | TIKU_154_CAP_ED_CCA)
#define TIKU_154_IPC_CHUNK 40u
#define TIKU_154_IPC_WINDOW_MS 20u
enum {
    TIKU_154_MODE = 16, TIKU_154_STAGE, TIKU_154_TX,
    TIKU_154_RX, TIKU_154_READ, TIKU_154_ED, TIKU_154_CCA
};
/** @brief One session's lease, staged frame and replayable transaction result. */
typedef struct {
    uint32_t session, generation, claim, last_id, expires;
    uint8_t request[64], reply[64], request_n, reply_n;
    uint8_t tx[125], rx[125], tx_n, rx_n, active;
} tiku_154_server_t;
/** @brief Encode a version-one IEEE 802.15.4 request or response header. */
void tiku_154_ipc_header(uint8_t *, uint8_t, uint32_t, uint32_t, uint8_t, uint8_t);
/** @brief Validate the complete inline message, including operation and reserved fields. */
int tiku_154_ipc_valid(const uint8_t *, unsigned, int);
/** @brief Expire ownership on lease timeout or session loss; preserve claim generations. */
void tiku_154_server_tick(tiku_154_server_t *, uint32_t session, uint32_t now, int ready);
/** @brief Execute once per monotonic identifier; return a cached response for exact retries. */
unsigned tiku_154_server_request(tiku_154_server_t *, const uint8_t *, unsigned, uint32_t now);
/** @brief Bind the initialized wire to its platform's client or server process. */
void tiku_154_service_start(tiku_ipc_wire_t *);
/** @brief Permit native HAL use by the server, or when no remote claim exists. */
int tiku_154_service_local_allowed(void);
#endif
