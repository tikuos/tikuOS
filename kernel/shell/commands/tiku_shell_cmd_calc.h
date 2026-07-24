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
 * Usage: calc N [op N]...
 *
 * Operators (space-separated tokens):
 *   +  -  *  /  %  min  max
 *
 * Arithmetic uses 32-bit signed integers; no floating point.
 * Operator precedence has two classes:
 *   - High (left-to-right):  *  /  %  min  max
 *   - Low  (left-to-right):  +  -
 *
 * The expression must alternate operand / operator / operand and is
 * bounded by the shell argv limit (TIKU_SHELL_MAX_ARGS = 8 tokens
 * including "calc"), so up to four operands and three operators.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_calc(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CALC_H_ */
