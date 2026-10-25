/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_port_hal.h - nonblocking independent UART byte access.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_UART_PORT_HAL_H_
#define TIKU_UART_PORT_HAL_H_
#include <stddef.h>
#include <stdint.h>
/**
 * @brief Byte operations on a configured, exclusively owned UART port.
 * @note Return copied bytes, zero for backpressure, or a negative error.
 * No caller pointer is retained. Bounded calls do not dispatch processes.
 */
typedef struct {
    int (*read)(void *, uint8_t *, size_t);
    int (*write)(void *, const uint8_t *, size_t);
    void *context;
} tiku_uart_port_io_t;
#endif
