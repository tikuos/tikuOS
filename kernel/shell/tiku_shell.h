/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell.h - interactive command-line interface (public types and API).
 *
 * Transport-agnostic: all I/O flows through the pluggable backend in
 * tiku_shell_io.h.  Declares the handler signature, the command-table entry type,
 * the sizing macros, the table accessor and tiku_shell_init().
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
 * TIKU_SHELL_LINE_SIZE - 1 so room is always left for the terminating NUL.
 *
 * @note Tier-gated: MSP430-class parts keep the lean 64 (small SRAM, and the
 *       history ring is FRAM-backed there); big-RAM parts get 256 so a whole
 *       command fits inline instead of being clipped.  Must stay <= 256 --
 *       cli.pos and the history indices are uint8_t, enforced by a
 *       _Static_assert in tiku_shell.c.  #ifndef so a build can override.
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
 * Bounds the argv array the parser fills (argv[0] is the command
 * name).  Tokens beyond this count are not parsed.
 */
#ifndef TIKU_SHELL_MAX_ARGS
#  ifdef PLATFORM_MSP430
#    define TIKU_SHELL_MAX_ARGS 8
#  else
/* 24, not 8: llm/pf-style commands take long id lists, and an argv cap
 * CLIPS SILENTLY -- six hours of timing data were once taken on commands
 * the parser had quietly truncated. */
#    define TIKU_SHELL_MAX_ARGS 24
#  endif
#endif

/**
 * @brief I/O poll interval in clock ticks.
 *
 * Period of the shell process's poll timer: the protothread wakes this often to
 * drain input and service jobs/rules.  TIKU_CLOCK_SECOND / 20 is roughly 50 ms,
 * responsive to typing yet rare enough to keep the CPU mostly idle.
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
 * For commands (like "help") that enumerate everything registered.  The array
 * includes category-header entries whose handler is NULL, so a caller must skip
 * those and stop at the sentinel whose name is NULL.
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

/*---------------------------------------------------------------------------*/
/* PUMPS -- work that must run in PROCESS context, once per shell pass        */
/*---------------------------------------------------------------------------*/
/**
 * @brief A callback the shell loop invokes once per pass.
 *
 * For drivers that need servicing with interrupts ENABLED and cannot live on
 * the scheduler's idle hook, which runs inside tiku_atomic_enter() -- a long
 * transfer there kills the tick, the console and the debugger's halt.
 *
 * @note The registry exists so drivers depend on the shell rather than the
 *       shell on any particular driver.  A pump runs on EVERY pass, so it must
 *       return promptly when it has nothing to do.
 */
typedef void (*tiku_shell_pump_fn)(void);

/**
 * @brief Register a pump.  Idempotent: re-registering the same function is a
 *        no-op that still reports success.
 * @return 0 on success, -1 if @p fn is NULL or the table is full.
 */
int tiku_shell_add_pump(tiku_shell_pump_fn fn);

/**
 * @brief Unregister a pump.  Unknown or NULL functions are ignored, so a
 *        driver may call this unconditionally on teardown.
 */
void tiku_shell_remove_pump(tiku_shell_pump_fn fn);

#if TIKU_SHELL_CMD_SLIP
/**
 * @brief Drain the shared UART through the SLIP demux from a blocking builtin.
 *
 * For a long-running builtin that busy-waits and thereby starves the shell's
 * main loop: call this in the wait loop so SLIP frames keep reaching the IP
 * stack.  Safe to call when no bytes are pending.
 *
 * @note Reuses the main loop's demux and its persistent frame buffer, so frames
 *       arriving across many calls reassemble correctly.
 */
void tiku_shell_net_pump(void);

/**
 * @brief SLIP-aware non-blocking getc for a blocking builtin that needs input.
 *
 * Like tiku_shell_net_pump(), but for a builtin that ALSO reads the keyboard
 * while a SLIP link is up.  Services the shared UART, routes SLIP frame bytes
 * to the IP stack, and returns the next genuine console byte or -1.
 *
 * @note Routing the frame bytes away is what stops a teardown or a
 *       retransmitted packet being mistaken for keystrokes and wedging the line
 *       editor.  Degenerates to a plain non-blocking getc when SLIP is inactive.
 */
int tiku_shell_net_getc(void);
#endif

#endif /* TIKU_SHELL_H_ */
