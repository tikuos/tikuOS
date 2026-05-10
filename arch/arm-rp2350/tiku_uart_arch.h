/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - UART backend for printf (RP2350 / PL011)
 *
 * Mirrors arch/msp430/tiku_uart_arch.h. Drives UART0 (PL011) with
 * an IRQ-fed RX ring buffer of 256 bytes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_UART_ARCH_H_
#define TIKU_RP2350_UART_ARCH_H_

#include <stdint.h>

void     tiku_uart_init(void);
void     tiku_uart_putc(char c);
void     tiku_uart_puts(const char *s);
void     tiku_uart_printf(const char *fmt, ...);
uint8_t  tiku_uart_rx_ready(void);
int      tiku_uart_getc(void);
uint16_t tiku_uart_overrun_count(void);
void     tiku_uart_overrun_reset(void);

#ifdef HAS_TESTS
void tiku_uart_test_inject(uint8_t byte);
#endif

#endif /* TIKU_RP2350_UART_ARCH_H_ */
