/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_console.h - "console" command: the line's channels and
 * counters.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CONSOLE_H_
#define TIKU_SHELL_CMD_CONSOLE_H_

#include <stdint.h>

/**
 * @brief List the console's channels and its frame counters.
 *
 * `console` prints the registered channels and the frame counters
 * (stray, oversize, phantom) since boot.  `console echo on` arms a link on
 * a spare channel that echoes every message it receives; `off` releases it.
 */
void tiku_shell_cmd_console(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CONSOLE_H_ */
