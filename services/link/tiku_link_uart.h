/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_uart.h - console-compatible framing on an independent UART.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LINK_UART_H_
#define TIKU_LINK_UART_H_
#include <services/link/tiku_link_console.h>
#include <hal/tiku_uart_port_hal.h>
#include <kernel/timers/tiku_clock.h>
#if (TIKU_IPC_ENABLE + 0)
/** @brief Bounded UART state; the envelope codec supplies CRC, ACK and sequence. */
typedef struct {
    tiku_link_t link;
    tiku_link_console_t codec;
    const tiku_uart_port_io_t *io;
    uint8_t tx[524], ack[12], rx[261];
    uint16_t tx_length, ack_length, tx_offset, rx_length;
    tiku_clock_time_t started;
    uint32_t bad;
    uint8_t active, framing, escaped, drop, closed;
} tiku_link_uart_t;
/** @brief Open an owned UART using the console envelope on one marked channel. */
tiku_link_t *tiku_link_uart_open(tiku_link_uart_t *context,
    const tiku_uart_port_io_t *io, uint8_t marker);
#endif
#endif
