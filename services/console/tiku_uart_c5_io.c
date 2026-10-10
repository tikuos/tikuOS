/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_uart_c5_io.c - C5 debug-output HAL routed to the UART0 console.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <arch/esp32c5/tiku_debug_arch.h>
#include <arch/esp32c5/tiku_uart_arch.h>
#include <stdarg.h>
#include <stdio.h>

void tiku_debug_arch_putc(char byte)
{
    tiku_uart_putc(byte);
}
int tiku_debug_arch_getc(void)
{
    return tiku_uart_getc();
}
uint8_t tiku_debug_arch_rx_ready(void)
{
    return tiku_uart_rx_ready();
}
void tiku_debug_arch_poll(void)
{
}
uint32_t tiku_debug_arch_dropped(void)
{
    tiku_c5_uart_stats_t stats;
    tiku_c5_uart_stats(&stats);
    return stats.tx_dropped;
}
void tiku_debug_arch_printf(const char *format, ...)
{
    char text[512];
    va_list args;
    unsigned i;
    if (format == NULL) {
        return;
    }
    va_start(args, format);
    (void)vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    for (i = 0; text[i]; i++) {
        if (text[i] == '\n') {
            tiku_uart_putc('\r');
        }
        tiku_uart_putc(text[i]);
    }
}
