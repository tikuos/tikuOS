/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_uart_arch.h - STM32F411RE UART console backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_UART_ARCH_H_
#define TIKU_STM32F411_UART_ARCH_H_

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

#endif /* TIKU_STM32F411_UART_ARCH_H_ */
