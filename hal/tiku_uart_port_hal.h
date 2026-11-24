/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_port_hal.h - nonblocking independent UART byte access.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_UART_PORT_HAL_H_
#define TIKU_UART_PORT_HAL_H_
#include <stddef.h>
#include <stdint.h>
/**
 * @brief Byte operations on a configured, exclusively owned UART port.
 * @note Return copied bytes, zero for backpressure, or a negative error.
 * No caller pointer is retained. Bounded calls do not dispatch processes.
 */
typedef struct {
    int (*read)(void *, uint8_t *, size_t);
    int (*write)(void *, const uint8_t *, size_t);
    void *context;
} tiku_uart_port_io_t;
/**
 * @brief Open the board's link UART, the one wired to its other processor.
 * @return Its byte operations, or NULL when the board refuses (the other
 *         processor unpowered) or has none.
 * @note Implemented by ports whose board declares a link UART (BOARD_CAPS
 *       LINK_UART) that the console does not use.  A second open while open
 *       returns the same operations.
 */
const tiku_uart_port_io_t *tiku_uart_link_open(void);
/** @brief Close the link UART and park its pins; buffered bytes are dropped. */
void tiku_uart_link_close(void);
/** @brief Received bytes the port saw lost or damaged since open: ring
 *         overflows, and the overruns, framing and parity errors its receiver
 *         reports (the MSP430's, which DMA reads, reports none). */
uint16_t tiku_uart_link_errors(void);
#endif
