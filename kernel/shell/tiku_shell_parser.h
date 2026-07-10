/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_parser.h - Command-line text parser
 *
 * Tokenizes a raw input line into argc/argv, looks up the first
 * token in the registered command table, and dispatches to the
 * matching handler.  Transport-agnostic — uses SHELL_PRINTF for any
 * output (unknown-command message, etc.).
 *
 * The interface is two calls: tiku_shell_parser_init() latches the
 * command table once at startup, and tiku_shell_parser_execute()
 * processes one assembled line.  Built-in commands are matched before
 * the FRAM-backed alias table, and alias bodies may chain commands
 * with ';'; both of those behaviours live in the implementation.  All
 * tokenisation is done in place in the caller's buffer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_PARSER_H_
#define TIKU_SHELL_PARSER_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell.h"          /* tiku_shell_cmd_t */

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Register the command table used for dispatch.
 *
 * Must be called once before tiku_shell_parser_execute().
 * The table must be NULL-terminated (last entry has name == NULL).
 *
 * @param commands  Pointer to a static, NULL-terminated command array
 */
void tiku_shell_parser_init(const tiku_shell_cmd_t *commands);

/**
 * @brief Parse and execute a complete input line.
 *
 * Tokenizes `line` in-place (inserts NUL bytes at space boundaries),
 * looks up argv[0] in the command table, and calls the handler.
 * Prints an error via SHELL_PRINTF if the command is not found.
 *
 * @param line  Mutable, NUL-terminated input string
 */
void tiku_shell_parser_execute(char *line);

#endif /* TIKU_SHELL_PARSER_H_ */
