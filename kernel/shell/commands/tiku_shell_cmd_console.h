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
 * `console` prints every registered channel with its match and buffer,
 * then the frames delivered per channel and the stray, oversize and
 * phantom counts since boot.
 */
void tiku_shell_cmd_console(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CONSOLE_H_ */
