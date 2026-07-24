/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - UART backend for printf (RP2350 / PL011)
 *
 * Mirrors arch/msp430/tiku_uart_arch.h. Drives UART0 (PL011) with
 * an IRQ-fed RX ring buffer of 256 bytes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_UART_ARCH_H_
#define TIKU_RP2350_UART_ARCH_H_

#include <stdint.h>

/**
 * @brief Initialize UART0 (PL011) at the board-defined baud rate.
 *
 * Unresets UART0, configures the integer and fractional baud-rate
 * divisors from clk_peri, enables the 8N1 FIFO, and installs the
 * NVIC handler for the RX IRQ that fills the 256-byte ring buffer.
 * Called once at early boot.
 */
void     tiku_uart_init(void);

/**
 * @brief Transmit one character, blocking until the TX FIFO has space.
 *
 * @param c  Character to send.
 */
void     tiku_uart_putc(char c);

/**
 * @brief Transmit a null-terminated string.
 *
 * @param s  String to send (must be null-terminated).
 */
void     tiku_uart_puts(const char *s);

/**
 * @brief Formatted output over UART0.
 *
 * Lightweight printf implemented without heap allocation.  Supports
 * %c, %s, %d, %u, %x, %lx, %lu, %ld.  Does not support floating-point
 * or width specifiers beyond basic use.
 *
 * @param fmt  Format string.
 * @param ...  Format arguments.
 */
void     tiku_uart_printf(const char *fmt, ...);

/**
 * @brief Check whether at least one byte is available in the RX ring.
 *
 * @return Non-zero if data is available, 0 if the ring is empty.
 */
uint8_t  tiku_uart_rx_ready(void);

/**
 * @brief Read one byte from the RX ring buffer.
 *
 * @return The received byte (0..255), or -1 if the ring is empty.
 */
int      tiku_uart_getc(void);

/**
 * @brief Return the number of RX overrun events since last reset.
 *
 * An overrun occurs when the 256-byte ring buffer is full and a new
 * byte arrives from the PL011 FIFO; the new byte is dropped.
 *
 * @return Overrun count (saturates at UINT16_MAX).
 */
uint16_t tiku_uart_overrun_count(void);

/**
 * @brief Reset the RX overrun counter to zero.
 */
void     tiku_uart_overrun_reset(void);

#ifdef HAS_TESTS
/**
 * @brief Inject a byte directly into the RX ring (test-only).
 *
 * Simulates a received byte without going through the hardware FIFO.
 * Available only when HAS_TESTS is defined.
 *
 * @param byte  Byte to inject.
 */
void tiku_uart_test_inject(uint8_t byte);
#endif

#endif /* TIKU_RP2350_UART_ARCH_H_ */
