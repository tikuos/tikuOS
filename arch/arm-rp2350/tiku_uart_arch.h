/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - UART backend for printf (RP2350 / PL011)
 *
 * Drives UART0 (PL011) with an interrupt-fed RX ring buffer of
 * TIKU_UART_RXBUF_SIZE bytes, 256 by default.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_UART_ARCH_H_
#define TIKU_RP2350_UART_ARCH_H_

#include <stdint.h>

/**
 * @brief Initialize UART0 (PL011) at the board-defined baud rate.
 *
 * Sets the integer and fractional baud divisors from clk_peri, enables 8N1
 * with FIFOs, and enables the RX interrupt that fills the ring buffer.
 *
 * @note UART0 must already be out of reset; the boot clock setup releases it.
 */
void     tiku_uart_init(void);

/** @brief Bounded drain before changing the peripheral clock. */
void tiku_rp2350_uart_drain(void);

/** @brief Recompute baud divisors without clearing queued receive data. */
void tiku_rp2350_uart_reclock(void);

/**
 * @brief Transmit one character, blocking until the TX FIFO has space.
 *
 * @param c  Character to send.
 */
void     tiku_uart_putc(char c);

/**
 * @brief Transmit a null-terminated string, sending '\n' as "\r\n".
 *
 * @param s  String to send; NULL sends nothing.
 */
void     tiku_uart_puts(const char *s);

/**
 * @brief Formatted output over UART0.
 *
 * Lightweight printf without heap allocation.  Supports %c, %s, %d, %u, %x,
 * %ld, %lu, %lx and %%, with a decimal width padded by '0' or spaces; no
 * floating point.
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
 * Counts bytes the PL011 flagged with OE and bytes dropped because the ring
 * buffer was full.
 *
 * @return Overrun count; it wraps at 65536.
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
 * Adds a received byte without the hardware FIFO; a full ring drops it.
 * Built only when HAS_TESTS is defined.
 *
 * @param byte  Byte to inject.
 */
void tiku_uart_test_inject(uint8_t byte);
#endif

#endif /* TIKU_RP2350_UART_ARCH_H_ */
