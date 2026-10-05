/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - UARTE console backend for printf (nRF54L, EasyDMA).
 *
 * The UARTE is DMA-only: TX sends one byte at a time from a bounce buffer, and
 * each received byte lands through a 1-byte DMA, re-armed by the ISR, in a
 * software ring.  Instance and pins come from the board header.
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

/** @brief Formatted output over the console UARTE (newlib-nano vsnprintf),
 *         truncated to 127 characters. */
void     tiku_uart_printf(const char *fmt, ...);

/** @brief Non-zero if a received byte is in the ring; an empty ring also
 *         runs the RX wedge check. */
uint8_t  tiku_uart_rx_ready(void);

/** @brief Read one byte from the RX ring: 0..255, or -1 when the ring is
 *         empty (does not block). */
int      tiku_uart_getc(void);

/** @brief Bytes lost to a full ring or a hardware overrun since init or
 *         the last reset; saturates at 0xFFFF. */
uint16_t tiku_uart_overrun_count(void);

/** @brief Reset the RX overrun counter. */
void     tiku_uart_overrun_reset(void);

/** @brief RX-engine wedge self-heals performed since boot (diagnostics). */
uint16_t tiku_uart_rx_recovery_count(void);

#endif /* TIKU_NORDIC_UART_ARCH_H_ */
