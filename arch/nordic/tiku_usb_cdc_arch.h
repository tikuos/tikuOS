/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usb_cdc_arch.h - nRF54LM20 USB CDC-ACM console.
 *
 * The same contract the RP2350's native USB console presents, so the
 * console core and the shell select it by TIKU_CONSOLE_USB alone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_USB_CDC_ARCH_H_
#define TIKU_USB_CDC_ARCH_H_

#include <stdint.h>

/** @brief Start the VBUS regulator; the stack comes up when a cable is seen. */
void tiku_usb_cdc_init(void);

/** @brief Bring the stack up or down as VBUS comes and goes, and push
 *         output the interrupt could not; called from the console's pump. */
void tiku_usb_cdc_poll(void);

/** @brief Non-zero once enumerated AND a terminal holds DTR. */
uint8_t tiku_usb_cdc_connected(void);

/** @brief Queue one character; a full ring drops the oldest, never blocks. */
void tiku_usb_cdc_putc(char c);

/** @brief Queue a string. */
void tiku_usb_cdc_puts(const char *s);

/** @brief Wait, bounded, until every queued byte has left. */
void tiku_usb_cdc_flush(void);

/** @brief Whether a byte from the host is waiting. */
uint8_t tiku_usb_cdc_rx_ready(void);

/** @brief The next byte from the host, or -1. */
int tiku_usb_cdc_getc(void);

/** @brief Bytes from the host lost to a full receive ring. */
uint16_t tiku_usb_cdc_overrun_count(void);
void     tiku_usb_cdc_overrun_reset(void);

#endif /* TIKU_USB_CDC_ARCH_H_ */
