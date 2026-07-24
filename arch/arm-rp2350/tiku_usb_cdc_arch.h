/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usb_cdc_arch.h - RP2350 native USB CDC-ACM console backend
 *
 * A from-scratch USB 1.1 full-speed device stack, just enough to present a
 * single CDC-ACM virtual serial port on the Pico 2's own USB connector --
 * so console/debug output can come over the same cable used for BOOTSEL
 * flashing, with no external FT232. It mirrors the tiku_uart_arch.* API so
 * the printf HAL and the pluggable shell I/O backend can select between UART
 * and USB at build time via the TIKU_CONSOLE make variable.
 *
 * The stack is POLLED (no IRQ): tiku_usb_cdc_poll() must be called often --
 * it is wired to the scheduler idle hook and is also nudged from putc/getc.
 *
 * Caveats (USB CDC is a dev/interactive convenience, not a low-power or CI
 * transport): the port re-enumerates across each BOOTSEL flash cycle, output
 * emitted before the host opens the port is dropped, and keeping the USB PHY
 * + 48 MHz clock alive costs power. Use UART (TIKU_CONSOLE=uart) for the
 * automated test loop and deployed low-power nodes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_USB_CDC_ARCH_H_
#define TIKU_USB_CDC_ARCH_H_

#include <stdint.h>
#include <kernel/shell/tiku_shell_io.h>

/*---------------------------------------------------------------------------*/
/* LIFECYCLE / SERVICE                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bring up PLL_USB (48 MHz), the USB controller and the CDC device,
 *        then connect the bus pull-up so the host begins enumeration.
 *
 * Safe to call once at boot. Does nothing useful until the host enumerates
 * the device and a terminal opens the port.
 */
void tiku_usb_cdc_init(void);

/**
 * @brief Service the USB device: bus reset, EP0 control/enumeration, and the
 *        bulk data endpoints. Must be called frequently (idle hook + putc).
 */
void tiku_usb_cdc_poll(void);

/**
 * @brief Non-zero once the host has enumerated the device AND a terminal has
 *        asserted DTR (opened the port). Output before this is discarded.
 */
uint8_t tiku_usb_cdc_connected(void);

/*---------------------------------------------------------------------------*/
/* OUTPUT (mirrors tiku_uart_arch.h)                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Queue one character for the host.
 *
 * On a full TX ring this polls the bus for a bounded window
 * (TX_FULL_WAIT_US) to let the host drain a slot rather than dropping
 * the byte outright.  If it is still full at the deadline the driver
 * latches an internal stalled flag so the rest of a burst drops fast
 * instead of paying the wait per byte — a host that stops reading
 * slows the console, it never freezes it.
 *
 * @param c  Character to queue
 */
void tiku_usb_cdc_putc(char c);

/**
 * @brief Queue a null-terminated string for the host.
 *
 * Feeds every character through tiku_usb_cdc_putc(), so it inherits the
 * same TX-ring back-pressure: when the ring is full the call waits a
 * bounded window for the host to drain a slot, and if the host is not
 * reading it latches a stalled flag and drops the rest of the burst
 * instead of freezing the console. No newline translation is done here.
 *
 * @param s  String to send; a NULL pointer is a no-op.
 */
void tiku_usb_cdc_puts(const char *s);

/**
 * @brief Formatted output over the CDC port (mirrors tiku_uart_printf).
 *
 * Lightweight formatter: no heap, no floating point. Supports %c, %s,
 * %d, %u and %x with an optional '0' pad flag, a field width, and the
 * 'l' length modifier (%ld, %lu, %lx); %% emits a literal percent sign
 * and an unrecognized conversion is echoed verbatim. A newline in the
 * literal format text is expanded to CRLF (text substituted by %s / %c
 * is not). Emits through tiku_usb_cdc_putc(), so the same TX-ring
 * back-pressure and drop-on-stalled-host behaviour applies.
 *
 * @param fmt  Format string; a NULL pointer is a no-op.
 * @param ...  Format arguments.
 */
void tiku_usb_cdc_printf(const char *fmt, ...);

/**
 * @brief Block (bounded) until the TX ring has drained to the host -- every
 *        queued byte has been pulled by an IN transaction. Call before a
 *        reset/reboot so the final packet (e.g. a test [TS:END] marker) is
 *        not truncated. The reboot path drains UART, not USB, so USB-console
 *        builds must flush explicitly. Returns early if the host is gone.
 */
void tiku_usb_cdc_flush(void);

/*---------------------------------------------------------------------------*/
/* INPUT (mirrors tiku_uart_arch.h)                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Non-zero if at least one received byte is waiting.
 *
 * Services the bus once before testing, so a byte the host has
 * already delivered is visible without an intervening poll.  Never
 * blocks.
 *
 * @return 1 if tiku_usb_cdc_getc() would return a byte, 0 otherwise.
 */
uint8_t  tiku_usb_cdc_rx_ready(void);

/**
 * @brief Read one byte from the RX ring. NON-BLOCKING.
 *
 * Services the bus first (tiku_usb_cdc_poll()) so bytes the host has
 * already delivered are picked up, then pops the oldest byte from the
 * 256-byte ring. It never waits for input: an empty ring returns the
 * -1 sentinel immediately, so callers poll rather than block.
 *
 * @return The received byte as 0..255, or -1 if no byte is available.
 */
int      tiku_usb_cdc_getc(void);

/**
 * @brief Return the number of received bytes dropped since the counter
 *        was last cleared.
 *
 * One overrun is counted per byte discarded because the 256-byte RX
 * ring was already full when a bulk-OUT packet arrived from the host,
 * i.e. the shell/application is not draining input fast enough. Purely
 * diagnostic: it is free-running (wraps at 65535, no saturation), is
 * zeroed by tiku_usb_cdc_init(), and reading it does not clear it.
 *
 * @return Dropped-byte count since init or the last overrun reset.
 */
uint16_t tiku_usb_cdc_overrun_count(void);

/**
 * @brief Clear the RX overrun counter back to zero.
 *
 * Diagnostic bookkeeping only -- it does not touch the RX ring or any
 * buffered data. Use it to bracket a measurement window.
 */
void     tiku_usb_cdc_overrun_reset(void);

/*---------------------------------------------------------------------------*/
/* SHELL I/O BACKEND                                                         */
/*---------------------------------------------------------------------------*/

/** CDC-ACM backend (echo + CRLF), selectable in place of tiku_shell_io_uart. */
extern const tiku_shell_io_t tiku_shell_io_usbcdc;

#endif /* TIKU_USB_CDC_ARCH_H_ */
