/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_alias.h - "alias" command
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_ALIAS_H_
#define TIKU_SHELL_CMD_ALIAS_H_

#include <stdint.h>

/**
 * @brief "alias" command — define or list shell shortcuts.
 *
 * The body is the rest of the line joined with single spaces, with surrounding
 * double quotes stripped and ';' chaining several commands.  Aliases live in
 * FRAM and survive reset; a built-in command always wins over an alias.
 */
void tiku_shell_cmd_alias(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ALIAS_H_ */
