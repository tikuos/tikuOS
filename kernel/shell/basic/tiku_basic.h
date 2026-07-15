/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic.h - public API of the Tiku BASIC interpreter engine.
 *
 * The engine is structured as a complex extension of the kernel
 * shell rather than a single shell command.  It owns its own
 * arena, FRAM-backed persistence, REPL, and embedded autorun
 * paths; the `basic` shell command is just a thin dispatch stub
 * over these entry points (see kernel/shell/commands/
 * tiku_shell_cmd_basic.{c,h}).
 *
 * Three entry points are exposed:
 *
 *   tiku_basic_repl()       - run the interactive REPL over the
 *                             active tiku_shell_io backend.
 *   tiku_basic_autorun()    - load the saved program from FRAM and
 *                             RUN it once (no REPL).
 *   tiku_basic_run_source() - parse a multi-line source string
 *                             (build-time BASIC_PROGRAM=foo.bas
 *                             firmware path) and RUN it.
 *
 * Plus the FRAM-backed persistence is also exposed as a VFS file
 * node so the saved program text can be read / written through the
 * normal `read /data/basic` / `write /data/basic` shell commands:
 *
 *   tiku_basic_vfs_read()   - read handler for /data/basic.
 *   tiku_basic_vfs_write()  - write handler for /data/basic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BASIC_H_
#define TIKU_BASIC_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* INTERPRETER ENTRY POINTS                                                  */
/*---------------------------------------------------------------------------*/

/*
 * BASIC as a non-blocking shell MODE.
 *
 * The interpreter is a mode of the shell process (like watch / ping /
 * mqtt), not a blocking takeover: `basic` enters the mode and returns,
 * and the shell poll loop drives it via these hooks.  The scheduler
 * therefore stays live for the whole session -- a running program yields
 * between batches of lines instead of freezing everything.
 */

/** @brief Enter the interactive REPL mode (the `basic` command). */
void tiku_basic_mode_enter(void);

/**
 * @brief Run the saved program headlessly as a non-blocking mode
 *        (the `basic run` command); returns 0 if a program started, else -1.
 */
int tiku_basic_mode_run_saved(void);

/**
 * @brief Resume (or first-boot start) the saved program headlessly as a
 *        non-blocking mode -- F1's power-failure-transparent autostart
 *        (the `basic run resume` command); returns 0 if running, else -1.
 */
int tiku_basic_mode_resume_saved(void);

/** @brief 1 while the shell is in BASIC mode (shell poll-loop hook). */
int tiku_basic_mode_active(void);

/** @brief Feed one console byte to the mode's line editor (poll-loop hook). */
void tiku_basic_mode_feed_char(int ch);

/** @brief Advance a running program by up to one batch of steps (poll-loop hook). */
void tiku_basic_mode_tick(void);

/**
 * @brief Notify BASIC that a watched VFS node changed (F2 event-driven
 *        ON CHANGE).  Call from the shell's TIKU_EVENT_VFS dispatch with the
 *        changed node pointer.  Safe to call always (no-op when the feature
 *        is compiled out or no program is running).
 */
void tiku_basic_mode_on_vfs(const void *node);

/**
 * @brief Consume the "BASIC mode just exited" edge (shell poll-loop hook).
 * @return 1 once after the mode leaves (so the shell reprints its prompt), else 0.
 */
int tiku_basic_mode_take_exit(void);

/**
 * @brief Load the persisted BASIC program from FRAM and RUN it once.
 *
 * Pair with the kernel init system (e.g. `init add 50 boot
 * 'basic run'`) to launch a saved program at every boot without
 * entering the REPL.  Returns silently if no program is saved.
 */
void tiku_basic_autorun(void);

/**
 * @brief Parse a multi-line BASIC source string and RUN it.
 *
 * Used by the build-time BASIC_PROGRAM=foo.bas mechanism: the .bas
 * file is converted to a NUL-terminated C string literal that is
 * baked into the firmware, and main.c calls this on boot.  Numbered
 * lines are stored; un-numbered direct commands (LIST / RUN / NEW
 * / SAVE / ...) execute immediately as they would at the REPL.  An
 * implicit RUN fires after parsing unless the source already issued
 * one explicitly.
 *
 * Pair with tiku_shell_io_set_backend() so PRINT output reaches the
 * active transport (UART, TCP, ...).
 *
 * @param source NUL-terminated source text; '\n' separates lines.
 */
void tiku_basic_run_source(const char *source);

/*---------------------------------------------------------------------------*/
/* VFS BRIDGE -- /data/basic file node                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the persisted BASIC program text into @p buf.
 *
 * Used as the read handler for the /data/basic VFS file node.
 *
 * @param buf  Destination buffer.
 * @param max  Capacity of @p buf in bytes.
 *
 * @return Number of bytes written (0 on no saved program), -1 on
 *         error.
 */
int tiku_basic_vfs_read(char *buf, unsigned int max);

/**
 * @brief Write @p len bytes of program text into the FRAM-backed
 *        persistent BASIC slot.
 *
 * Used as the write handler for the /data/basic VFS file node.
 * Each line should be a numbered BASIC statement separated by '\n',
 * exactly as `LIST` emits.
 *
 * @param buf  Source buffer (numbered BASIC source text).
 * @param len  Number of bytes to write.
 *
 * @return 0 on success, -1 on error.
 */
int tiku_basic_vfs_write(const char *buf, unsigned int len);

/**
 * @brief Error sink callback: receives every interpreter error (A5).
 *
 * @param cat  Error category, one of TIKU_BASIC_ERR_* (tiku_basic_config.h).
 * @param msg  Bare message text (no color codes, no "? " prefix, no newline).
 */
typedef void (*tiku_basic_error_sink_t)(int cat, const char *msg);

/**
 * @brief Install a custom error sink so BASIC can run headless.
 *
 * By default interpreter errors print to the shell console as a red
 * "? message".  Installing a sink redirects them to a buffer/callback
 * instead, so BASIC can run with no shell or UART attached (the
 * agent/library direction).  Pass NULL to restore the console default.
 *
 * @param sink  Callback to receive errors, or NULL for the default.
 */
void tiku_basic_set_error_sink(tiku_basic_error_sink_t sink);

#endif /* TIKU_BASIC_H_ */
