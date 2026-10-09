/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_serial_jtag_io.c - C5 buffered shell and debug output over native USB.
 * Foreground-only output drops bytes after a 20 ms host-backpressure timeout.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_usb_serial_jtag.h"
#include <arch/esp32c5/tiku_debug_arch.h>
#include <arch/esp32c5/tiku_esp32c5_regs.h>
#include <arch/esp32c5/tiku_rom_arch.h>
#include <stdarg.h>
#include <stdio.h>

static uint8_t output[1024];
static size_t head, count;
static uint32_t dropped;
static uint8_t stalled;

/** @brief Read the wrapping CPU cycle counter for a bounded transmit wait. */
#ifndef TIKU_C5_CONSOLE_CYCLES
static uint32_t cycles(void)
{
    uint32_t value;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(value));
    return value;
}
#define TIKU_C5_CONSOLE_CYCLES() cycles()
#endif
void tiku_serial_jtag_poll(void)
{
    size_t contiguous, accepted;
    if (count == 0) { tiku_usb_serial_jtag_poll(); return; }
    contiguous = sizeof(output) - head;
    if (contiguous > count) { contiguous = count; }
    accepted = tiku_usb_serial_jtag_write(output + head, contiguous);
    head = (head + accepted) % sizeof(output);
    count -= accepted;
    if (accepted) { stalled = 0; }
    if (count == 0) { tiku_usb_serial_jtag_poll(); }
}
void tiku_serial_jtag_putc(char byte)
{
    uint32_t start = TIKU_C5_CONSOLE_CYCLES();
    if (count == sizeof(output)) {
        do {
            tiku_serial_jtag_poll();
        } while (!stalled && count == sizeof(output) &&
                 (uint32_t)(TIKU_C5_CONSOLE_CYCLES() - start) < tiku_c5_rom_cpu_frequency() * 20000u);
        if (count == sizeof(output)) { dropped++; stalled = 1; return; }
    }
    output[(head + count) % sizeof(output)] = (uint8_t)byte;
    count++;
    if (byte == '\n' || count >= 64) { tiku_serial_jtag_poll(); }
}
uint8_t tiku_serial_jtag_rx_ready(void)
{
    tiku_serial_jtag_poll();
    return (TIKU_C5_REG_READ(TIKU_C5_USB_EP1_CONF) & TIKU_C5_USB_RX_AVAIL) != 0;
}
uint32_t tiku_serial_jtag_dropped(void) { return dropped; }
void tiku_debug_arch_putc(char byte) { tiku_serial_jtag_putc(byte); }
int tiku_debug_arch_getc(void) { return tiku_usb_serial_jtag_getc(); }
uint8_t tiku_debug_arch_rx_ready(void) { return tiku_serial_jtag_rx_ready(); }
uint32_t tiku_debug_arch_dropped(void) { return dropped; }
void tiku_debug_arch_poll(void) { tiku_serial_jtag_poll(); }
void tiku_debug_arch_printf(const char *format, ...)
{
    char text[512];
    va_list args;
    unsigned i;
    uint32_t start;
    va_start(args, format);
    (void)vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    for (i = 0; text[i]; i++) {
        if (text[i] == '\n') { tiku_serial_jtag_putc('\r'); }
        tiku_serial_jtag_putc(text[i]);
    }
    start = TIKU_C5_CONSOLE_CYCLES();
    do {
        tiku_serial_jtag_poll();
    } while (count && !stalled &&
             (uint32_t)(TIKU_C5_CONSOLE_CYCLES() - start) < tiku_c5_rom_cpu_frequency() * 20000u);
}
