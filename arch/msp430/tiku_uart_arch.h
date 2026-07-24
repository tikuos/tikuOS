/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - UART backend for printf (MSP430 architecture)
 *
 * Provides a compiler-aware UART printf backend:
 *   - GCC (msp430-elf-gcc): Routes printf through eUSCI_A0 UART
 *     using the LaunchPad backchannel (USB-to-serial on debugger).
 *   - CCS (cl430): No-op; CIO semihosting handles printf via JTAG.
 *
 * Call tiku_uart_init() during boot, after clock and GPIO are ready.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_UART_ARCH_H_
#define TIKU_UART_ARCH_H_

#include <stdint.h>

/**
 * @brief Initialize the UART peripheral for printf output.
 *
 * Under GCC: configures eUSCI_A0 for 9600 baud @ 8 MHz SMCLK
 * using the board-specific backchannel UART pins.
 * Under CCS: no-op (CIO handles printf).
 */
void tiku_uart_init(void);

/**
 * @brief Transmit a single character over UART.
 *
 * Under GCC: blocking write to eUSCI_A0 TX buffer.
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
 * Supports: %s, %d, %u, %x, %c, %%, optional field width (e.g. %4d),
 * and long modifier (e.g. %ld, %4ld). Lightweight (~60 bytes of stack).
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
 * Call tiku_uart_rx_ready() first, or check the return value.
 * Reading the hardware register clears the RX-ready flag.
 *
 * @return The received character (0-255), or -1 if none available
 */
int tiku_uart_getc(void);

/**
 * @brief Return the number of hardware UART overruns since init.
 *
 * An overrun (UCOE) occurs when a new byte arrives before the
 * previous one was read from the shift register.  The ISR counts
 * each occurrence so firmware and tests can detect transport
 * reliability problems.
 *
 * @return Cumulative overrun count (reset to 0 by tiku_uart_init)
 */
uint16_t tiku_uart_overrun_count(void);

/**
 * @brief Zero the overrun counter without re-initialising the UART.
 *
 * Useful for scoping a "no overruns during this phase" assertion to
 * a specific window. The drain loops in the loopback / ringbuf
 * tests intentionally swallow a backlog of host-echoed printf bytes
 * which (on bursty USB-CDC bridges like FT232 with no flow control)
 * can register one or two UCOE events before the test's binary
 * phase even starts. Call this immediately after the resync so the
 * counter only reflects what the binary phase actually does.
 *
 * Atomic against the RX ISR.
 */
void tiku_uart_overrun_reset(void);

/**
 * @brief Inject one byte into the RX ring buffer (test only).
 *
 * Allows unit tests to feed bytes into the UART receive path
 * without the RX ISR running.  Only available when HAS_TESTS=1.
 *
 * Not part of the normal driver path: nothing in the shell or the
 * console backend calls it, and no hardware is touched.  The byte
 * appears to tiku_uart_rx_ready() / tiku_uart_getc() exactly as a
 * received one would.  If the ring is full the byte is dropped
 * silently.  Under the CCS/TI build this is a no-op stub.
 *
 * @param byte Byte to inject
 */
#ifdef HAS_TESTS
void tiku_uart_test_inject(uint8_t byte);
#endif

#endif /* TIKU_UART_ARCH_H_ */
