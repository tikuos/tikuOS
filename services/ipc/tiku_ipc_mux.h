/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_mux.h - logical message links sharing a physical carrier.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_MUX_H_
#define TIKU_IPC_MUX_H_
#include <services/link/tiku_link.h>
#include <kernel/ipc/tiku_ipc_config.h>
#if TIKU_IPC_ENABLE
#define TIKU_IPC_MUX_CHANNELS 8u
struct tiku_ipc_mux;
/** @brief A channel's link and an optional physical-loss callback. */
typedef struct {
    tiku_link_t link;
    struct tiku_ipc_mux *owner;
    void (*lost)(void *);
    void *lost_context;
    uint8_t id, used;
} tiku_ipc_mux_channel_t;
/** @brief One physical receiver and eight independently opened logical links. */
typedef struct tiku_ipc_mux {
    tiku_link_t *physical;
    tiku_ipc_mux_channel_t channels[TIKU_IPC_MUX_CHANNELS];
    uint32_t dropped;
} tiku_ipc_mux_t;
/** @brief Install the physical receiver; the caller owns this carrier exclusively. */
int tiku_ipc_mux_init(tiku_ipc_mux_t *, tiku_link_t *physical);
/** @brief Open an agreed channel id; frame payloads are at most 255 bytes. */
tiku_link_t *tiku_ipc_mux_open(tiku_ipc_mux_t *, uint8_t channel,
    void (*lost)(void *), void *context);
/** @brief Notify attached logical endpoints after a physical reset or loss. */
void tiku_ipc_mux_lost(tiku_ipc_mux_t *);
#endif
#endif
