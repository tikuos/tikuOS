/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - ESP32-C61 console on UART0, interrupt-fed receive.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_UART_ARCH_H_
#define TIKU_ESP32C61_UART_ARCH_H_

#include <stdint.h>

/** @brief Ready UART0 for the console (8N1 at TIKU_BOARD_UART_BAUD). */
void tiku_uart_init(void);

/** @brief Send one character, waiting for FIFO room (bounded). */
void tiku_uart_putc(char c);

/** @brief Send a string, newlines as CR LF. */
void tiku_uart_puts(const char *s);

/** @brief Small printf: %d %u %x %c %s %%, l and zero-padded widths. */
void tiku_uart_printf(const char *fmt, ...);

/** @brief 1 when a received byte is waiting. */
uint8_t tiku_uart_rx_ready(void);

/** @brief Next received byte, or -1 when there is none. */
int tiku_uart_getc(void);

/** @brief Receive overruns since the last reset of the counter. */
uint16_t tiku_uart_overrun_count(void);

/** @brief Zero the overrun counter. */
void tiku_uart_overrun_reset(void);

#ifdef HAS_TESTS
/** @brief Queue one byte as if it had been received (test hook). */
void tiku_uart_test_inject(uint8_t byte);
#endif

#endif /* TIKU_ESP32C61_UART_ARCH_H_ */
