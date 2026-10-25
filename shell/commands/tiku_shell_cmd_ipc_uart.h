/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ipc_uart.h - independent framed UART0 loopback qualification.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_CMD_IPC_UART_H_
#define TIKU_SHELL_CMD_IPC_UART_H_
/** @brief Exercise UART0 loopback at 115200 baud, then restore the board default. */
void tiku_shell_ipc_uart(void);
#endif
