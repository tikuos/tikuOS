/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - STM32N6 console on USART1, the ST-LINK virtual COM port.
 *
 * TX is PE5 and RX is PE6, both alternate function 7.  Transmit is polled with
 * a bounded wait, so output works from any context, a fault handler included.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_UART_ARCH_H_
#define TIKU_STM32N6_UART_ARCH_H_

#include <stdint.h>

/**
 * @brief Bring up USART1 at the board's console baud rate.
 *
 * Clocks USART1 from HSI, so the baud divisor does not change with the bus
 * clocks.
 */
void tiku_uart_init(void);

/**
 * @brief Write one character, waiting for room in the transmit FIFO.
 *
 * After 2000000 polls without room the character is dropped.
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
 * Supports %s %c %d %u %x %% with an optional 0 flag, width and 'l'
 * modifier, and expands '\n' to CR LF; newlib's printf is not used.
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

/**
 * @brief Count of receive overruns since the counter was last zeroed.
 *
 * @return Overruns observed, saturating at 0xFFFF
 */
uint16_t tiku_uart_overrun_count(void);

/** @brief Zero the overrun counter. */
void tiku_uart_overrun_reset(void);

#ifdef HAS_TESTS
/**
 * @brief Push a byte into the receive path without hardware.
 *
 * @param byte  Byte to deliver to the next tiku_uart_getc()
 */
void tiku_uart_test_inject(uint8_t byte);
#endif

#endif /* TIKU_STM32N6_UART_ARCH_H_ */
