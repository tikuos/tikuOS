/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_console.h - "console" command: the console's channels and
 * counters.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CONSOLE_H_
#define TIKU_SHELL_CMD_CONSOLE_H_

#include <stdint.h>

/**
 * @brief "console" command: print the console's channels and counters.
 *
 * Prints one line per registered channel, the decoder's totals and the
 * console link's counters since boot.  `console echo [on|off]` opens or
 * closes a link on channel 0xF2 that sends every message it receives back.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_console(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CONSOLE_H_ */
