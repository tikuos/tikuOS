/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell.h - interactive command-line interface (public types and API).
 *
 * Output goes through the pluggable backend in tiku_shell_io.h.  Declares the
 * handler signature, the command-table entry type, the sizing macros, the
 * table accessor, tiku_shell_init() and the pump registry.
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
 * @brief Size of the input line buffer, NUL included.
 *
 * Typed input stops at TIKU_SHELL_LINE_SIZE - 1 characters, and each history
 * entry is this size.  64 on MSP430, where the history ring is in FRAM, and
 * 256 elsewhere; a build may override it.
 *
 * @note At most 256: the editor's position and the tab-completion lengths are
 *       uint8_t, which a _Static_assert in tiku_shell.c checks.
 */
#ifndef TIKU_SHELL_LINE_SIZE
#  ifdef PLATFORM_MSP430
#    define TIKU_SHELL_LINE_SIZE  64
#  else
#    define TIKU_SHELL_LINE_SIZE  256
#  endif
#endif

/**
 * @brief Maximum number of space-separated arguments per command.
 *
 * Sizes the argv array the parser fills; argv[0] is the command name.  Tokens
 * past the limit are dropped without an error.  8 on MSP430, 24 elsewhere.
 */
#ifndef TIKU_SHELL_MAX_ARGS
#  ifdef PLATFORM_MSP430
#    define TIKU_SHELL_MAX_ARGS 8
#  else
#    define TIKU_SHELL_MAX_ARGS 24
#  endif
#endif

/**
 * @brief I/O poll interval in clock ticks, about 50 ms.
 *
 * Period of the shell process's poll timer; each expiry drains input and runs
 * the pumps, jobs and rules.
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
 * An entry with a NULL handler is a category header, and a sentinel entry
 * with name == NULL marks the end of the table.
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
 * Category-header entries in it have a NULL handler; a caller listing the
 * commands skips them and stops at the entry whose name is NULL.
 *
 * @return Pointer to the first element of the static command table.
 */
const tiku_shell_cmd_t *tiku_shell_get_commands(void);

/**
 * @brief The shell process control block, defined in tiku_shell.c.
 *
 * The rules engine, `watch` and BASIC subscribe it to VFS watches, and `kill`
 * refuses to stop it.
 */
extern struct tiku_process tiku_shell_process;

/**
 * @brief Register the shell process with the scheduler.
 *
 * A TCP-only shell also registers the net process; a TIKU_SHELL_NET_TEST
 * build also starts UDP and TCP and registers the CoAP server.
 *
 * @note Call once from main(), before the scheduler loop starts.
 */
void tiku_shell_init(void);

/*---------------------------------------------------------------------------*/
/* PUMPS                                                                     */
/*---------------------------------------------------------------------------*/
/**
 * @brief A callback the shell loop calls once per pass, in process context
 *        with interrupts enabled.
 *
 * For driver work that needs interrupts enabled while it runs, such as a USB
 * mass-storage data phase.
 *
 * @note A pump runs on every pass and must return promptly when it has
 *       nothing to do.
 */
typedef void (*tiku_shell_pump_fn)(void);

/**
 * @brief Register a pump; registering one already in the table changes
 *        nothing and succeeds.
 * @return 0 on success, -1 if @p fn is NULL or all TIKU_SHELL_PUMP_MAX slots
 *         are taken.
 */
int tiku_shell_add_pump(tiku_shell_pump_fn fn);

/**
 * @brief Unregister a pump.  A function that is not registered, or NULL, is
 *        ignored.
 */
void tiku_shell_remove_pump(tiku_shell_pump_fn fn);

#if TIKU_SHELL_CMD_SLIP
/**
 * @brief Drain the console wire from a builtin that busy-waits.
 *
 * Runs tiku_console_pump(): each frame waiting on the wire goes to its channel,
 * SLIP to the IP stack among them, and text goes to the console's text sink or
 * is dropped.  A frame that arrives across several calls is reassembled.
 */
void tiku_shell_net_pump(void);
#endif

/**
 * @brief Non-blocking getc for a builtin that reads input while it holds the
 *        shell loop.
 *
 * Kicks the watchdog, then reads as the line editor does: the telnet client
 * while the TCP backend is active, else tiku_console_getc(), which hands every
 * frame met on the way to its channel.
 *
 * @return The next byte, or -1 when none is waiting or no backend is set.
 */
int tiku_shell_net_getc(void);

#endif /* TIKU_SHELL_H_ */
