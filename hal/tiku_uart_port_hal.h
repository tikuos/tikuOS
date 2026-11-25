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
/**
 * @brief Name a function for the link UART's interrupt to call when it has
 *        received the byte @p end, and when it has sent what a write took.
 * @param hook the function, or NULL for none; it runs in the interrupt
 * @param end  the byte that ends a frame on this link
 * @note A port whose receiver has no interrupt per byte keeps the hook and
 *       calls nothing: its link is read on the tick.
 */
void tiku_uart_link_on_event(void (*hook)(void), uint8_t end);
/** @brief Received bytes the port saw lost or damaged since open: ring
 *         overflows, and the overruns, framing and parity errors its receiver
 *         reports (the MSP430's, which DMA reads, reports none). */
uint16_t tiku_uart_link_errors(void);
/**
 * @brief Set the link UART's speed in bits a second.  A closed port keeps it
 *        for its next open; an open one changes once it has sent what its
 *        writes took, and a byte arriving meanwhile may be lost.
 * @return 0 once set, 1 while it is still sending (call again), or -1 for a
 *         speed the port has no setting for
 */
int tiku_uart_link_set_baud(uint32_t baud);
/** @brief 1 when the link UART has a setting for @p baud, else 0. */
int tiku_uart_link_baud_ok(uint32_t baud);
/** @brief The link UART's speed in bits a second: the board's
 *         TIKU_BOARD_LINK_BAUD until tiku_uart_link_set_baud() changes it,
 *         and 0 on a board without a link UART. */
uint32_t tiku_uart_link_baud(void);
#endif
