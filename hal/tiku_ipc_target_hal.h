/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_target_hal.h - byte-driven packet target hooks for port interrupts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_TARGET_HAL_H_
#define TIKU_IPC_TARGET_HAL_H_
#include <hal/tiku_ipc_packet_hal.h>
#if TIKU_IPC_ENABLE
/** @brief Port-owned target state; byte hooks run in its serialized IRQ handler. */
typedef struct {
    const uint8_t *tx;
    uint8_t *rx;
    uint16_t capacity;
    volatile uint16_t sent, received;
    volatile uint8_t armed, fault;
    uint8_t spi;
    uint8_t (*quiesce)(void *); /**< Mask/stop target access before returning 1. */
    void *port;
} tiku_ipc_target_t;
/** @brief Arm validated packet staging; exported as a packet-IO start callback. */
tiku_ipc_err_t tiku_ipc_target_start(void *, const void *, void *, uint16_t);
/** @brief Complete after the required bytes; errors need abort confirmation. */
tiku_ipc_err_t tiku_ipc_target_poll(void *);
/** @brief Retain pointers until the port confirms it cannot issue another access. */
uint8_t tiku_ipc_target_abort(void *);
/** @brief SPI IRQ byte: consume MOSI and return the next MISO byte. */
uint8_t tiku_ipc_target_shift(tiku_ipc_target_t *, uint8_t incoming);
/** @brief I²C target IRQ: accept one write byte; zero rejects excess data. */
uint8_t tiku_ipc_target_write(tiku_ipc_target_t *, uint8_t incoming);
/** @brief I²C target IRQ: provide one read byte, or 0xff without an armed byte. */
uint8_t tiku_ipc_target_read(tiku_ipc_target_t *);
/** @brief Mark a short STOP/chip-select transaction as a failed packet. */
void tiku_ipc_target_end(tiku_ipc_target_t *);
#endif
#endif
