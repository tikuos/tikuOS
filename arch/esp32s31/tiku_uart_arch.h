/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - S31 UART0 console with interrupt-buffered receive.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32S31_UART_ARCH_H_
#define TIKU_ESP32S31_UART_ARCH_H_
#include <stdint.h>
/** @brief Initialize GPIO58/59 UART0 at 115200, 8N1, using the 40 MHz crystal. */
void tiku_uart_init(void);
/** @brief Send one byte with a bounded FIFO wait; discard it on timeout. */
void tiku_uart_putc(char byte);
/** @brief Return nonzero when the 1023-byte receive queue has data. */
uint8_t tiku_uart_rx_ready(void);
/** @brief Return the next queued byte, or minus one when the queue is empty. */
int tiku_uart_getc(void);
/** @brief Return the saturating count of lost bytes and UART error events. */
uint16_t tiku_uart_overrun_count(void);
/** @brief Clear the receive error counter with interrupts masked. */
void tiku_uart_overrun_reset(void);
#endif
