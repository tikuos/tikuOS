/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_clear.h - "clear" command: ANSI clear screen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CLEAR_H_
#define TIKU_SHELL_CMD_CLEAR_H_

#include <stdint.h>

/**
 * @brief "clear" command: erase the terminal and home the cursor.
 *
 * Writes ESC[2J then ESC[H; a viewer without ANSI support shows those bytes
 * as they are.  The arguments are ignored.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_clear(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CLEAR_H_ */
