/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.h - Console backend for printf (Apollo 510)
 *
 * Mirrors arch/arm-rp2350/tiku_uart_arch.h so hal/tiku_printf_hal.h
 * routes TIKU_PRINTF -> tiku_uart_printf unchanged. The Apollo510
 * console is SWO/ITM (not a wire UART): the implementation backs these
 * entry points onto am_hal_itm + am_util_stdio_printf at bring-up (the
 * same path the user's hello_world example uses). The "uart" naming is
 * kept only to satisfy the printf HAL contract; a real COM-UART backend
 * (pins 30/55) can be selected later.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_UART_ARCH_H_
#define TIKU_AMBIQ_UART_ARCH_H_

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

#endif /* TIKU_AMBIQ_UART_ARCH_H_ */
