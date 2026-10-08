/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usb_cdc_arch.h - RP2350 native USB CDC-ACM console backend.
 *
 * Presents one CDC-ACM port on the Pico 2's own USB connector, with the same
 * calls as the UART driver; the build picks one of the two as the console.
 * The stack is polled: the bus is served only while tiku_usb_cdc_poll() runs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_USB_CDC_ARCH_H_
#define TIKU_USB_CDC_ARCH_H_

#include <stdint.h>
#include <shell/tiku_shell_io.h>

/*---------------------------------------------------------------------------*/
/* LIFECYCLE / SERVICE                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bring up PLL_USB (48 MHz), the USB controller and the CDC device,
 *        then connect the bus pull-up so the host begins enumeration.
 *
 * Polls the bus until the host configures the device or 4,000,000 polls pass.
 *
 * @note Call once at boot.
 */
void tiku_usb_cdc_init(void);

/**
 * @brief Service the USB device: bus reset, EP0 control/enumeration, and the
 *        bulk data endpoints.
 *
 * @note Call it often: the idle hook, putc, getc and rx_ready call it.
 */
void tiku_usb_cdc_poll(void);

/**
 * @brief Non-zero once the host has configured the device and a terminal has
 *        asserted DTR (opened the port).
 *
 * Bytes written before configuration wait in the TX ring; once it is full they
 * are dropped.
 */
uint8_t tiku_usb_cdc_connected(void);

/*---------------------------------------------------------------------------*/
/* OUTPUT                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Queue one character for the host.
 *
 * On a full TX ring, polls the bus for up to 2 ms for a free slot.  If none
 * frees, the byte is dropped, and so is each following byte, at once, while
 * the ring stays full.
 *
 * @param c  Character to queue
 */
void tiku_usb_cdc_putc(char c);

/**
 * @brief Queue a null-terminated string for the host.
 *
 * Feeds every character through tiku_usb_cdc_putc(), inheriting the same
 * TX-ring back-pressure and drop-on-stalled-host behaviour.  No newline
 * translation happens here.
 *
 * @param s  String to send; a NULL pointer is a no-op.
 */
void tiku_usb_cdc_puts(const char *s);

/**
 * @brief Formatted output over the CDC port.
 *
 * No heap, no floating point.  Supports %c, %s, %d, %u and %x with an optional
 * '0' pad flag, a field width (ignored by %c and %s) and the 'l' modifier; %%
 * emits a literal percent and an unrecognized conversion is echoed verbatim.
 *
 * @note A newline in the literal format text expands to CRLF, though text
 *       substituted by %s / %c does not.  Emits through tiku_usb_cdc_putc(), so
 *       the same TX-ring back-pressure applies.
 * @param fmt  Format string; a NULL pointer is a no-op.
 * @param ...  Format arguments.
 */
void tiku_usb_cdc_printf(const char *fmt, ...);

/**
 * @brief Poll until the host has taken every queued byte, or 4,000,000 polls
 *        pass.
 *
 * @note Call before a reset so the last packet is not lost; the reboot path
 *       drains the UART, not USB.
 */
void tiku_usb_cdc_flush(void);

/*---------------------------------------------------------------------------*/
/* INPUT                                                                     */
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
 * @brief Read one byte from the RX ring without blocking.
 *
 * Services the bus first via tiku_usb_cdc_poll() so bytes the host already
 * delivered are picked up, then pops the oldest byte from the 256-byte ring.
 *
 * @return The received byte as 0..255, or -1 if no byte is available.
 */
int      tiku_usb_cdc_getc(void);

/**
 * @brief Return the number of received bytes dropped since the counter
 *        was last cleared.
 *
 * One overrun per byte discarded because the 256-byte RX ring was already full
 * when a bulk-OUT packet arrived.
 *
 * @note Wraps at 65536, is zeroed by tiku_usb_cdc_init(), and reading it does
 *       not clear it.
 * @return Dropped-byte count since init or the last overrun reset.
 */
uint16_t tiku_usb_cdc_overrun_count(void);

/**
 * @brief Clear the RX overrun counter back to zero.
 *
 * Leaves the RX ring and its buffered data as they are.
 */
void     tiku_usb_cdc_overrun_reset(void);

/*---------------------------------------------------------------------------*/
/* SHELL I/O BACKEND                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief CDC-ACM shell backend (echo + CRLF, full VFS authority), selectable
 *        in place of tiku_shell_io_uart.
 */
extern const tiku_shell_io_t tiku_shell_io_usbcdc;

#endif /* TIKU_USB_CDC_ARCH_H_ */
