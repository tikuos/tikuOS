/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_bus.h - packet links with retained DMA staging.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_BUS_H_
#define TIKU_IPC_BUS_H_
#include <services/link/tiku_link.h>
#include <hal/tiku_ipc_packet_hal.h>
#if TIKU_IPC_ENABLE
#define TIKU_IPC_PACKET_SLOT 320u
/** @brief Fixed bus staging; no private IPC mapping is exposed to hardware. */
typedef struct {
    uint8_t tx[TIKU_IPC_PACKET_SLOT] __attribute__((aligned(64)));
    uint8_t rx[TIKU_IPC_PACKET_SLOT] __attribute__((aligned(64)));
    uint8_t pending[258];
    tiku_link_t link;
    const tiku_ipc_packet_io_t *io;
    tiku_clock_time_t deadline;
    uint32_t failed;
    uint8_t queued, active, closing, quarantined, initialized;
} tiku_ipc_bus_t;
/** @brief Validate both staging spans before installing the packet engine. */
tiku_link_t *tiku_ipc_bus_open(tiku_ipc_bus_t *bus, const tiku_ipc_packet_io_t *io);
/** @brief True after close when the engine can no longer access the context. */
uint8_t tiku_ipc_bus_reusable(const tiku_ipc_bus_t *bus);
#endif
#endif
