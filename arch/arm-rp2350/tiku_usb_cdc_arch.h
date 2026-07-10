/*
 * Tiku Operating System v0.05
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

void tiku_usb_cdc_putc(char c);
void tiku_usb_cdc_puts(const char *s);
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

uint8_t  tiku_usb_cdc_rx_ready(void);
int      tiku_usb_cdc_getc(void);
uint16_t tiku_usb_cdc_overrun_count(void);
void     tiku_usb_cdc_overrun_reset(void);

/*---------------------------------------------------------------------------*/
/* SHELL I/O BACKEND                                                         */
/*---------------------------------------------------------------------------*/

/** CDC-ACM backend (echo + CRLF), selectable in place of tiku_shell_io_uart. */
extern const tiku_shell_io_t tiku_shell_io_usbcdc;

#endif /* TIKU_USB_CDC_ARCH_H_ */
