/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_alias.h - "alias" command
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_ALIAS_H_
#define TIKU_SHELL_CMD_ALIAS_H_

#include <stdint.h>

/**
 * @brief "alias" command — define or list shell shortcuts.
 *
 * Usage:
 *   alias                       — list all defined aliases
 *   alias <name> <body...>      — define / overwrite an alias
 *
 * The body is the rest of the line, joined with single spaces.
 * Surrounding double quotes are stripped, so both forms work:
 *   alias temp read /dev/temp0
 *   alias temp "read /dev/temp0"
 *
 * Use ';' inside the body to chain commands:
 *   alias status "ps; free; read /sys/power/source"
 *
 * Aliases live in FRAM and survive reset / power loss. Built-in
 * commands always win, so an alias cannot shadow 'help', 'reboot',
 * or any other registered command.
 */
void tiku_shell_cmd_alias(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ALIAS_H_ */
