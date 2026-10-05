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
 * @brief "alias" command: list the aliases, or define one.
 *
 * Usage: alias [<name> <body...>].  The body is the rest of the line joined
 * by single spaces; ';' separates commands in it.  Aliases are kept in
 * durable memory, and a built-in command of the same name runs instead.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_alias(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ALIAS_H_ */
