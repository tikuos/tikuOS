/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_parser.h - command-line text parser.
 *
 * Two calls: init latches the command table once, execute processes one
 * assembled line.  Transport-agnostic, and all tokenisation happens in place
 * in the caller's buffer.
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
 * The table must be NULL-terminated (last entry has name == NULL).
 *
 * @note Call once, before tiku_shell_parser_execute().
 * @param commands  Pointer to a static, NULL-terminated command array
 */
void tiku_shell_parser_init(const tiku_shell_cmd_t *commands);

/**
 * @brief Parse and execute a complete input line.
 *
 * Tokenises the line in place (NULs at token ends; a quoted span is one
 * token), looks up argv[0] in the command table and then the alias table, and
 * prints an error if neither matches.
 *
 * @param line  Mutable, NUL-terminated input string
 * @note A line exceeding TIKU_SHELL_MAX_ARGS is rejected without dispatch.
 */
void tiku_shell_parser_execute(char *line);

#endif /* TIKU_SHELL_PARSER_H_ */
