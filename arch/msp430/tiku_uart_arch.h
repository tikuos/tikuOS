/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
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
 * Takes the oldest byte from the RX ring, which a DMA channel fills from
 * the receive register (tiku_uart_dma_arch.h).
 *
 * @return The received character (0-255), or -1 if none available
 */
int tiku_uart_getc(void);

/** @brief This driver calls a hook from an interrupt when bytes wait. */
#define TIKU_UART_HAS_RX_HOOK 1

/**
 * @brief Call @p fn when received bytes wait in the ring; NULL ends the
 *        calls.
 *
 * The call comes once the line has been quiet for a byte after a start bit,
 * and at each start bit while the ring is half full.
 * @note @p fn runs in interrupt context, and that interrupt leaves LPM3 on
 *       exit.  The wake is timed by Timer A0, so it needs the system tick.
 */
void tiku_uart_rx_hook(void (*fn)(void));

/**
 * @brief Return the number of received bytes lost since init.
 *
 * Bytes are lost when the ring's writer passes its reader: the unread ones
 * are dropped and counted, by this call too.  The DMA takes each byte from
 * RXBUF, so none is lost to a late interrupt.
 *
 * @return Cumulative count, stopping at 0xFFFF (reset to 0 by tiku_uart_init)
 */
uint16_t tiku_uart_overrun_count(void);

/**
 * @brief Zero the count of lost bytes without re-initialising the UART.
 *
 * Lets a test count from a point of its choosing.
 */
void tiku_uart_overrun_reset(void);

/**
 * @brief Hand the receive path one byte without the hardware (test only).
 *
 * The byte reads back as a received one would, ahead of the line's.  It is
 * dropped when 63 injected bytes are unread.
 *
 * @note Built only with HAS_TESTS; the TI build compiles it to a stub.
 *
 * @param byte Byte to inject
 */
#ifdef HAS_TESTS
void tiku_uart_test_inject(uint8_t byte);
#endif

#endif /* TIKU_UART_ARCH_H_ */
