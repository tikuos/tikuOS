/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - STM32N6 console on USART1, the ST-LINK virtual COM port.
 *
 * TX is PE5 and RX is PE6, both alternate function 7. Transmit is polled, so
 * output is safe from any context including a fault handler.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_UART_ARCH_H_
#define TIKU_STM32N6_UART_ARCH_H_

#include <stdint.h>

/**
 * @brief Bring up USART1 at the board's console baud rate.
 *
 * Clocks USART1 from HSI so the baud divisor does not depend on whatever the
 * boot ROM left in the bus clock tree.
 */
void tiku_uart_init(void);

/**
 * @brief Write one character, blocking until the transmit register is free.
 *
 * @param c  Character to send
 */
void tiku_uart_putc(char c);

/**
 * @brief Write a NUL-terminated string, expanding '\n' to CR LF.
 *
 * @param s  String to send; NULL is ignored
 */
void tiku_uart_puts(const char *s);

/**
 * @brief Formatted output over USART1.
 *
 * Supports %s %c %d %u %x %% with an optional width and 'l' modifier, which
 * covers the kernel's output without linking newlib's printf.
 *
 * @param fmt  Format string; NULL is ignored
 */
void tiku_uart_printf(const char *fmt, ...);

/**
 * @brief Report whether a received byte is waiting.
 *
 * @return 1 when a byte can be read, 0 otherwise
 */
uint8_t tiku_uart_rx_ready(void);

/**
 * @brief Read one received byte without blocking.
 *
 * @return The byte, or -1 when none is waiting
 */
int tiku_uart_getc(void);

#endif /* TIKU_STM32N6_UART_ARCH_H_ */
