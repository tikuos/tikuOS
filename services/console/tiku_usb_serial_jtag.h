/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_usb_serial_jtag.h - polled ESP32-C5 USB Serial/JTAG byte transport.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_USB_SERIAL_JTAG_H_
#define TIKU_USB_SERIAL_JTAG_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Disable USB interrupts; retain the ROM's USB PHY configuration. */
void tiku_usb_serial_jtag_init(void);

/**
 * @brief Submit up to 64 bytes, returning the accepted count without waiting.
 * @note Serialize callers; retry the unaccepted suffix after zero/partial writes.
 * @return Zero for a full FIFO, NULL data or zero length. Does not promise delivery.
 */
size_t tiku_usb_serial_jtag_write(const uint8_t *data, size_t length);

/**
 * @brief Complete a full-packet transfer with a zero-length packet when writable.
 * @note Call when no payload remains queued; serialize with writes. Does not wait.
 */
void tiku_usb_serial_jtag_poll(void);

/** @brief Return one byte, or -1 when the USB receive FIFO is empty. */
int tiku_usb_serial_jtag_getc(void);

/** @brief Flush pending console bytes without waiting; foreground context only. */
void tiku_serial_jtag_poll(void);
/** @brief Queue a byte with a bounded wait when full; count dropped bytes on timeout. */
void tiku_serial_jtag_putc(char byte);
/** @brief Flush pending output and test whether the receive FIFO contains a byte. */
uint8_t tiku_serial_jtag_rx_ready(void);
/** @brief Return console bytes dropped because the host did not accept output. */
uint32_t tiku_serial_jtag_dropped(void);

#endif
