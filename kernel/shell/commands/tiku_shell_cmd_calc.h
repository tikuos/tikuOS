/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_calc.h - "calc" command: integer arithmetic
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CALC_H_
#define TIKU_SHELL_CMD_CALC_H_

#include <stdint.h>

/**
 * @brief "calc" command — evaluate an infix integer expression.
 *
 * Space-separated tokens over 32-bit signed integers, no floating point.
 * Two precedence classes, both left-to-right: * / % min max bind tighter
 * than + -.  The shell argv limit bounds it to four operands.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_calc(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CALC_H_ */
