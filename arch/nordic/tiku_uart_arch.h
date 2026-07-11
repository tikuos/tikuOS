/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - UARTE console backend for printf (nRF54L, EasyDMA)
 *
 * Mirrors arch/arm-rp2350/tiku_uart_arch.h.  The nRF54L UARTE is DMA-only
 * (EasyDMA), so TX transmits a byte at a time from a small RAM bounce buffer
 * and RX arms a single-byte DMA that re-arms on each read.  The UARTE
 * instance and pins come from the board header (TIKU_BOARD_CONSOLE_UARTE,
 * TIKU_BOARD_UART_TX_PORT/PIN, ...).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_UART_ARCH_H_
#define TIKU_NORDIC_UART_ARCH_H_

#include <stdint.h>

/** @brief Configure + enable the console UARTE at TIKU_BOARD_UART_BAUD. */
void     tiku_uart_init(void);

/** @brief Transmit one character, blocking until the DMA transfer completes. */
void     tiku_uart_putc(char c);

/** @brief Transmit a null-terminated string. */
void     tiku_uart_puts(const char *s);

/** @brief Formatted output over the console UARTE (newlib-nano vsnprintf). */
void     tiku_uart_printf(const char *fmt, ...);

/** @brief Non-zero if a received byte is available. */
uint8_t  tiku_uart_rx_ready(void);

/** @brief Read one byte (blocking); returns 0..255. */
int      tiku_uart_getc(void);

/** @brief RX overrun count (bytes lost between reads); saturates. */
uint16_t tiku_uart_overrun_count(void);

/** @brief Reset the RX overrun counter. */
void     tiku_uart_overrun_reset(void);

/** @brief RX-engine wedge self-heals performed since boot (diagnostics). */
uint16_t tiku_uart_rx_recovery_count(void);

#endif /* TIKU_NORDIC_UART_ARCH_H_ */
