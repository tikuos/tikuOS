/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_parser.h - Command-line text parser
 *
 * Tokenizes a raw input line into argc/argv, looks up the first
 * token in the registered command table, and dispatches to the
 * matching handler.  Transport-agnostic — uses CLI_PRINTF for any
 * output (unknown-command message, etc.).
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CLI_PARSER_H_
#define TIKU_CLI_PARSER_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_cli.h"          /* tiku_cli_cmd_t */

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Register the command table used for dispatch.
 *
 * Must be called once before tiku_cli_parser_execute().
 * The table must be NULL-terminated (last entry has name == NULL).
 *
 * @param commands  Pointer to a static, NULL-terminated command array
 */
void tiku_cli_parser_init(const tiku_cli_cmd_t *commands);

/**
 * @brief Parse and execute a complete input line.
 *
 * Tokenizes `line` in-place (inserts NUL bytes at space boundaries),
 * looks up argv[0] in the command table, and calls the handler.
 * Prints an error via CLI_PRINTF if the command is not found.
 *
 * @param line  Mutable, NUL-terminated input string
 */
void tiku_cli_parser_execute(char *line);

#endif /* TIKU_CLI_PARSER_H_ */
