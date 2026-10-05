/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - Ambiq console UART, the backend of TIKU_PRINTF.
 *
 * The printf HAL routes TIKU_PRINTF here.  tiku_uart_arch.c implements it for
 * the Apollo510 and tiku_uart_apollo4l.c for the Apollo4 Lite and Plus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_UART_ARCH_H_
#define TIKU_AMBIQ_UART_ARCH_H_

#include <stdint.h>

/**
 * @brief Initialize the console transport (wire UART).
 *
 * Brings up the board's COM UART (see the board header for pads and FUNCSEL)
 * for 8N1 at TIKU_BOARD_UART_BAUD, with interrupt-driven RX.
 *
 * @note Called during boot before any printf output, and again after the
 *       UART's power domain has been off.
 */
void     tiku_uart_init(void);

/**
 * @brief Transmit a single character over the console.
 *
 * Blocks until the UART TX FIFO has room.  Used internally by
 * tiku_uart_puts() and tiku_uart_printf().
 *
 * @param c  Character to transmit.
 */
void     tiku_uart_putc(char c);

/**
 * @brief Transmit a null-terminated string over the console.
 *
 * Calls tiku_uart_putc() for each character until the null terminator,
 * sending LF as CR+LF.  A NULL @p s sends nothing.
 *
 * @param s  Null-terminated string to transmit.
 */
void     tiku_uart_puts(const char *s);

/**
 * @brief Fault-safe putc: like tiku_uart_putc() but with a bounded wait.
 *
 * For use from fault handlers only. Drops the character instead of
 * spinning forever when the transmitter has stopped draining.
 *
 * @param c  Character to transmit.
 */
void     tiku_uart_fault_putc(char c);

/**
 * @brief Wait (bounded) until the TX FIFO is empty and the line is idle.
 *
 * For use from fault handlers before a reset, so queued diagnostic
 * output is not destroyed by the reset.
 */
void     tiku_uart_fault_drain(void);

/**
 * @brief Formatted print over the console (printf-style).
 *
 * Implements the TIKU_PRINTF contract required by hal/tiku_printf_hal.h with
 * a small built-in formatter: %c, %s, %d, %u, %x (with an optional l), %% and
 * zero or space padding to a width.  LF goes out as CR+LF.
 *
 * @param fmt  printf-style format string.
 * @param ...  Variadic arguments matching the format specifiers.
 */
void     tiku_uart_printf(const char *fmt, ...);

/**
 * @brief Check whether a received byte is waiting in the RX buffer.
 *
 * @return Non-zero if at least one byte is available to read,
 *         0 if the RX buffer is empty.
 */
uint8_t  tiku_uart_rx_ready(void);

/**
 * @brief Read one byte from the console RX buffer.
 *
 * @return The received byte as an unsigned value (0..255), or -1 if
 *         no byte is available.
 */
int      tiku_uart_getc(void);

/**
 * @brief Return the count of RX overrun events since the last reset.
 *
 * Counts hardware RX FIFO overruns and bytes dropped because the RX ring
 * buffer was full.
 *
 * @return Number of RX overrun events.
 */
uint16_t tiku_uart_overrun_count(void);

/** @brief Clear the RX overrun counter. */
void     tiku_uart_overrun_reset(void);

#ifdef HAS_TESTS
/**
 * @brief Inject a byte into the RX buffer (test use only).
 *
 * Simulates a received byte without requiring real hardware input.
 * Available only when HAS_TESTS is defined.
 *
 * @param byte  Byte value to inject into the RX buffer.
 */
void tiku_uart_test_inject(uint8_t byte);
#endif

#endif /* TIKU_AMBIQ_UART_ARCH_H_ */
