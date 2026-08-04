/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - RA8P1 console on SCI8, the kit's virtual COM port.
 *
 * TX is PD02 and RX is PD03, both peripheral-select 00100b (TXD8_C/RXD8_C).
 * Transmit is polled, so output is safe from any context including a fault
 * handler.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_UART_ARCH_H_
#define TIKU_RA8P1_UART_ARCH_H_

#include <stdint.h>

#include <arch/ra8p1/tiku_device_select.h>

/**
 * @brief Bring up SCI8 at the board's console baud rate.
 *
 * The divisor is computed from the device's boot PCLKA constant, which is the
 * measured 8 MHz MOCO.  When R4 changes the clock this must be re-run, so it
 * is safe to call more than once.
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
 * @brief Formatted output over the console.
 *
 * Supports %s %c %d %u %x %% with an optional 'l' modifier, which covers the
 * kernel's output without linking newlib's printf.
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
 * @brief Count of receive overruns since the counter was last cleared.
 *
 * @return Overruns observed
 */
uint16_t tiku_uart_overrun_count(void);

/** @brief Zero the overrun counter. */
void tiku_uart_overrun_reset(void);

#endif /* TIKU_RA8P1_UART_ARCH_H_ */
