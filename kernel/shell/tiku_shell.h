/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell.h - Interactive command-line interface (public types and API)
 *
 * The CLI is transport-agnostic: all I/O flows through the pluggable
 * backend in tiku_shell_io.h (UART today, network / LLM channel later).
 *
 * This header declares the pieces other translation units need: the
 * command-handler signature and command-table entry type
 * (tiku_shell_cmd_t), the line/argument sizing macros, the accessor
 * that hands out the static command table, and the one-call service
 * entry point tiku_shell_init().  The table itself, the line editor,
 * and the individual command handlers live in tiku_shell.c and the
 * commands/ directory.
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
#include "tiku_shell_config.h"   /* SH_* color macros, command flags */
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Maximum characters in a single input line (including the
 *        NUL terminator).
 *
 * Sizes the line editor's cli.buf; typed input is capped at
 * TIKU_SHELL_LINE_SIZE - 1 characters so room is always left for the
 * terminating NUL the parser expects.
 */
#define TIKU_SHELL_LINE_SIZE  64

/**
 * @brief Maximum number of space-separated arguments per command.
 *
 * Bounds the argv array the parser fills (argv[0] is the command
 * name).  Tokens beyond this count are not parsed.
 */
#define TIKU_SHELL_MAX_ARGS   8

/**
 * @brief I/O poll interval in clock ticks.
 *
 * Period of the shell process's poll timer: the protothread wakes
 * this often to drain input and service jobs/rules.  TIKU_CLOCK_SECOND
 * / 20 is roughly 50 ms — responsive to typing yet rare enough to
 * keep the CPU mostly idle.
 */
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
 * registered commands.  The returned array includes category-header
 * entries whose handler is NULL; callers that walk it must skip those
 * and stop at the sentinel whose name is NULL.
 *
 * @return Pointer to the first element of the static command table.
 */
const tiku_shell_cmd_t *tiku_shell_get_commands(void);

/**
 * @brief The shell process control block.
 *
 * Defined in tiku_shell.c via TIKU_PROCESS().  Exposed so callers
 * (and tiku_shell_init()) can register it with the scheduler.
 */
extern struct tiku_process tiku_shell_process;

/**
 * @brief Initialize and register the shell process.
 *
 * Registers the shell via tiku_process_register() so it coexists
 * with any autostart processes (apps, examples).  Call from main()
 * before entering the scheduler loop.
 */
void tiku_shell_init(void);

#if TIKU_SHELL_CMD_SLIP
/**
 * @brief Drain the shared UART through the SLIP demux from a blocking builtin.
 *
 * For use by a long-running builtin (e.g. BASIC HTTPGET$) that busy-waits and
 * thereby starves the shell's main loop: call this in the wait loop so SLIP
 * frames keep reaching the IP stack.  Reuses the main loop's demux (and its
 * persistent frame buffer), so frames arriving across many calls reassemble
 * correctly.  No-op-safe to call when no bytes are pending.
 */
void tiku_shell_net_pump(void);

/**
 * @brief SLIP-aware non-blocking getc for a blocking builtin that needs input.
 *
 * Like tiku_shell_net_pump(), but for a builtin that ALSO reads the keyboard
 * while a SLIP link is up (e.g. the BASIC REPL / INPUT after a BROWSE).  It
 * services the shared UART, routes any SLIP frame bytes to the IP stack -- so a
 * connection's teardown and late/retransmitted packets drain to the stack
 * instead of being mistaken for keystrokes and wedging the line editor -- and
 * returns the next genuine console byte, or -1 if none is pending.  Degenerates
 * to a plain non-blocking getc when SLIP is inactive.
 */
int tiku_shell_net_getc(void);
#endif

#endif /* TIKU_SHELL_H_ */
