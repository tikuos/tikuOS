/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_parser.h - command-line text parser.
 *
 * Two calls: init latches the command table once, execute processes one assembled
 * line.  Transport-agnostic, and all tokenisation happens in place in the
 * caller's buffer.
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
