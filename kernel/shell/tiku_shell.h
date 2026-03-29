/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell.h - Interactive command-line interface (public types and API)
 *
 * The CLI is transport-agnostic: all I/O flows through the pluggable
 * backend in tiku_shell_io.h (UART today, network / LLM channel later).
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

#ifndef TIKU_SHELL_H_
#define TIKU_SHELL_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"
#include "tiku_shell_io.h"       /* SHELL_PRINTF, I/O backend API */
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Maximum characters in a single input line */
#define TIKU_SHELL_LINE_SIZE  64

/** Maximum number of space-separated arguments per command */
#define TIKU_SHELL_MAX_ARGS   8

/** I/O poll interval (ticks). TIKU_CLOCK_SECOND/20 ~ 50 ms */
#define TIKU_SHELL_POLL_TICKS (TIKU_CLOCK_SECOND / 20)

/*---------------------------------------------------------------------------*/
/* TYPES                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Command handler function signature
 *
 * @param argc  Number of arguments (including the command name)
 * @param argv  Argument strings (argv[0] is the command name)
 */
typedef void (*tiku_shell_handler_t)(uint8_t argc, const char *argv[]);

/**
 * @brief Command table entry
 *
 * A sentinel entry with name == NULL marks the end of the table.
 */
typedef struct {
    const char *name;               /**< Command name (typed by user) */
    const char *help;               /**< One-line description */
    tiku_shell_handler_t handler;     /**< Handler function */
} tiku_shell_cmd_t;

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the NULL-terminated command table.
 *
 * Useful for commands (like "help") that need to enumerate all
 * registered commands.
 */
const tiku_shell_cmd_t *tiku_shell_get_commands(void);

/** The shell process */
extern struct tiku_process tiku_shell_process;

/**
 * @brief Initialize and register the shell process.
 *
 * Registers the shell via tiku_process_register() so it coexists
 * with any autostart processes (apps, examples).  Call from main()
 * before entering the scheduler loop.
 */
void tiku_shell_init(void);

#endif /* TIKU_SHELL_H_ */
