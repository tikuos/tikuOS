/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_debug_arch.h - P4 HAL debug-output contract.
 *
 * hal/tiku_printf_hal.h routes debug output to these calls;
 * services/console/tiku_serial_jtag_io.c implements them over the USB
 * Serial/JTAG peripheral.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_DEBUG_ARCH_H_
#define TIKU_ESP32P4_DEBUG_ARCH_H_
#include <stdint.h>
/** @brief Format at most 511 debug bytes and submit them to the configured
 * console. */
void tiku_debug_arch_printf(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
/** @brief Submit one raw console byte with a bounded wait for transport
 * capacity. */
void tiku_debug_arch_putc(char byte);
/** @brief Return one raw receive byte, or -1 when none is queued. */
int tiku_debug_arch_getc(void);
/** @brief Test for receive data and service pending output. */
uint8_t tiku_debug_arch_rx_ready(void);
/** @brief Return the count of console bytes rejected by the transmit transport.
 */
uint32_t tiku_debug_arch_dropped(void);
/** @brief Service queued console output without waiting; UART output needs no
 * polling. */
void tiku_debug_arch_poll(void);
#endif
