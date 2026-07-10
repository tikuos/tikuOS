/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_slip.h - "slip" command: hand the console UART to SLIP/IP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_SLIP_H_
#define TIKU_SHELL_CMD_SLIP_H_

#include <stdint.h>

/**
 * @brief "slip" command: start the net process and hand it the console UART.
 *
 * Switches the shell into SLIP mode: the net process
 * (tiku_kits_net_process) takes over the UART for binary SLIP/IP framing
 * and the shell stops reading input.  Reset the board to return to the
 * interactive shell.
 *
 * Requires the TikuKits net stack (TIKU_KIT_NET_ENABLE=1); the command is
 * compiled in only when the stack is present.
 *
 * @param argc  Argument count (including the command name)
 * @param argv  Argument strings (argv[0] is the command name)
 */
void tiku_shell_cmd_slip(uint8_t argc, const char *argv[]);

/**
 * @brief Whether SLIP mode is active (the net process owns the UART).
 *
 * The shell loop calls this to yield UART input to the net process while
 * SLIP mode is engaged, instead of consuming it as line-editor keystrokes.
 *
 * @return 1 if SLIP mode is active, 0 otherwise.
 */
uint8_t tiku_shell_cmd_slip_active(void);

/**
 * @brief Turn SLIP/IP mode on (idempotent), bringing up the link.
 *
 * Used by other net commands (e.g. ping) to ensure the shared RX demux is
 * routing SLIP frames to the IP stack before they send traffic.
 */
void tiku_shell_cmd_slip_enable(void);

#endif /* TIKU_SHELL_CMD_SLIP_H_ */
