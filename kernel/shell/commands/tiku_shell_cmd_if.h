/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_if.h - "if" command: conditional VFS-driven action
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_IF_H_
#define TIKU_SHELL_CMD_IF_H_

#include <stdint.h>

/**
 * @brief "if" command — read a VFS path, compare it to a value,
 *        and dispatch a sub-command on match.
 *
 * Usage:
 *   if <path> <op> <value> <command...>
 *
 * Operators:
 *   ==  !=             string or numeric
 *   >   <   >=  <=     numeric only
 *
 * Examples:
 *   if /dev/temp0 > 40 write /dev/led0 1
 *   if /sys/power/source == battery write /dev/led1 1
 *
 * Numeric comparison is attempted first; if either side fails to
 * parse as an integer, the command falls back to string compare
 * (which then only accepts == and !=).
 *
 * The sub-command is dispatched through the same shell parser, so
 * any registered command works on the right-hand side. Recursion
 * is bounded to keep stack usage finite on small MCUs.
 */
void tiku_shell_cmd_if(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_IF_H_ */
