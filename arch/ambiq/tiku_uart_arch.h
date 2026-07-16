/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - Console backend for printf (Apollo 510)
 *
 * Mirrors arch/arm-rp2350/tiku_uart_arch.h so hal/tiku_printf_hal.h
 * routes TIKU_PRINTF -> tiku_uart_printf unchanged. The Apollo510
 * console is SWO/ITM (not a wire UART): the implementation backs these
 * entry points onto am_hal_itm + am_util_stdio_printf at bring-up (the
 * same path the user's hello_world example uses). The "uart" naming is
 * kept only to satisfy the printf HAL contract; a real COM-UART backend
 * (pins 30/55) can be selected later.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_UART_ARCH_H_
#define TIKU_AMBIQ_UART_ARCH_H_

#include <stdint.h>

/**
 * @brief Initialize the console transport (SWO/ITM or wire UART).
 *
 * Configures the ITM stimulus port and enables SWO output via
 * am_hal_itm. Called once during boot before any printf output.
 * A real COM-UART backend (TX=pad 30, RX=pad 55) may be substituted
 * by reconfiguring this entry point.
 */
void     tiku_uart_init(void);

/**
 * @brief Transmit a single character over the console.
 *
 * Blocks until the ITM FIFO accepts the byte. Used internally by
 * tiku_uart_puts() and tiku_uart_printf().
 *
 * @param c  Character to transmit.
 */
void     tiku_uart_putc(char c);

/**
 * @brief Transmit a null-terminated string over the console.
 *
 * Calls tiku_uart_putc() for each character until the null terminator.
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
 * Implements the TIKU_PRINTF contract required by hal/tiku_printf_hal.h.
 * Backed by am_util_stdio_printf at bring-up; the format-string subset
 * supported depends on the am_util implementation.
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
 * An overrun occurs when a received byte is dropped because the RX
 * buffer was full. Non-zero values indicate the console is producing
 * data faster than the application is consuming it.
 *
 * @return Number of RX overrun events.
 */
uint16_t tiku_uart_overrun_count(void);

/**
 * @brief Clear the RX overrun counter.
 *
 * Resets the overrun count to zero. Call after logging or handling
 * the overrun condition.
 */
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
