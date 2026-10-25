/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_controller.h - packet exchanges over the portable controller buses.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_CONTROLLER_H_
#define TIKU_IPC_CONTROLLER_H_
#include <services/ipc/tiku_ipc_bus.h>
#if TIKU_IPC_ENABLE
/** @brief Controller-side packet state; the caller configures and owns the bus. */
typedef struct {
    tiku_ipc_packet_io_t io;
    uint8_t address, spi, pending;
    tiku_ipc_err_t result;
    void (*select)(void *, uint8_t active);
    void *select_context;
} tiku_ipc_controller_t;
/**
 * @brief Use blocking portable SPI exchanges under an owned chip select.
 * @note The domain names a CPU-copy engine. burst_ticks bounds a complete
 * port transaction, including its timeout path; calibrate it per port.
 */
int tiku_ipc_controller_spi(tiku_ipc_controller_t *context,
    const tiku_ipc_access_domain_t *domain, uint32_t peer,
    tiku_clock_time_t burst_ticks, void (*select)(void *, uint8_t), void *select_context);
/** @brief Use one I²C write/repeated-start/read exchange with a seven-bit peer. */
int tiku_ipc_controller_i2c(tiku_ipc_controller_t *context,
    const tiku_ipc_access_domain_t *domain, uint32_t peer,
    tiku_clock_time_t burst_ticks, uint8_t address);
#endif
#endif
