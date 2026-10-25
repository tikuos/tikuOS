/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_port_arch.h - independent UART0 byte port for framed links.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_UART_PORT_ARCH_H_
#define TIKU_P4_UART_PORT_ARCH_H_
#include <hal/tiku_uart_port_hal.h>
/** @brief UART0 byte operations; caller owns its configuration until link close. */
const tiku_uart_port_io_t *tiku_p4_uart_port_io(void);
#endif
