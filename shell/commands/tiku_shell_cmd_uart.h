/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_uart.h - "uart" command: the link UART to the other processor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_UART_H_
#define TIKU_SHELL_CMD_UART_H_

#include <stdint.h>

/**
 * @brief "uart" command -- the board's link UART (hal/tiku_uart_port_hal.h).
 *
 * `open`, `close`, `send <word>...` (the words and a CR), `recv`, `relay`
 * (console to the link and back until Ctrl-]), `echo <count>` (return what
 * arrives) and `test <count>` (send a known sequence, check its echo).
 */
void tiku_shell_cmd_uart(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_UART_H_ */
