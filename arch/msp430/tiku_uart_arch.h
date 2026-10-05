/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - UART backend for printf (MSP430).
 *
 * The kernel UART behind printf.  Under GCC it drives the board's
 * backchannel eUSCI_A; under CCS every call is a stub and CIO semihosting
 * carries printf.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_UART_ARCH_H_
#define TIKU_UART_ARCH_H_

#include <stdint.h>

/**
 * @brief Initialize the UART peripheral for printf output.
 *
 * Under GCC: configures the eUSCI_A that TIKU_BOARD_UART_MODULE selects at
 * TIKU_BOARD_UART_BAUD, 8N1, on the board's backchannel pins, empties the
 * RX ring and enables the RX interrupt.  Under CCS: no-op.
 *
 * @note Call during boot, once the clock and GPIO are set up.
 */
void tiku_uart_init(void);

/**
 * @brief Transmit a single character over UART.
 *
 * Under GCC: blocking write to the selected eUSCI_A TX buffer.
 * Under CCS: no-op.
 *
 * @param c Character to transmit
 */
void tiku_uart_putc(char c);

/**
 * @brief Transmit a null-terminated string over UART.
 *
 * Converts bare '\\n' to '\\r\\n' for terminal compatibility.
 *
 * @param s String to transmit
 */
void tiku_uart_puts(const char *s);

/**
 * @brief Lightweight printf replacement for UART output.
 *
 * Supports %s, %d, %u, %x, %c and %%, a field width (e.g. %4d) and the
 * long modifier (e.g. %ld, %4ld).  A newline goes out as CR LF; any other
 * conversion prints as written.
 */
void tiku_uart_printf(const char *fmt, ...);

/**
 * @brief Check whether a received character is available.
 *
 * Non-blocking. Returns non-zero if tiku_uart_getc() will succeed.
 *
 * @return 1 if a character is ready, 0 otherwise
 */
uint8_t tiku_uart_rx_ready(void);

/**
 * @brief Read one character from the UART (non-blocking).
 *
 * Takes the oldest byte from the RX ring that the UART ISR fills.
 *
 * @return The received character (0-255), or -1 if none available
 */
int tiku_uart_getc(void);

/**
 * @brief Return the number of hardware UART overruns since init.
 *
 * An overrun is a byte that arrived before the previous one was read from
 * RXBUF; the RX ISR counts each one.  Bytes dropped because the software
 * ring was full are not counted.
 *
 * @return Cumulative overrun count (reset to 0 by tiku_uart_init)
 */
uint16_t tiku_uart_overrun_count(void);

/**
 * @brief Zero the overrun counter without re-initialising the UART.
 *
 * Lets a test count overruns from a point of its choosing, such as after
 * its drain loop has swallowed echoed bytes.
 */
void tiku_uart_overrun_reset(void);

/**
 * @brief Inject one byte into the RX ring buffer (test only).
 *
 * Feeds the receive path without the ISR or the hardware; the byte then
 * reads back as a received one would.  A full ring drops it.
 *
 * @note Built only with HAS_TESTS; the TI build compiles it to a stub.
 *
 * @param byte Byte to inject
 */
#ifdef HAS_TESTS
void tiku_uart_test_inject(uint8_t byte);
#endif

#endif /* TIKU_UART_ARCH_H_ */
