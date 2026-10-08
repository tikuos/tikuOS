/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic.h - public API of the Tiku BASIC interpreter engine.
 *
 * The engine owns its arena, its durable storage and its REPL; the `basic`
 * shell command dispatches to the entry points declared here.
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
 * BASIC as a shell mode.
 *
 * The interpreter runs as a mode of the shell process, as watch, ping and
 * mqtt do: `basic` enters the mode and returns, and the shell poll loop
 * drives it through these hooks.  A running program yields to the scheduler
 * between batches of lines.
 */

/**
 * @brief Enter the interactive REPL mode (the `basic` command).
 *
 * Begins a session, prints the banner and the first prompt, and returns; the
 * shell poll loop drives the mode.  While the mode is active it reports
 * "BASIC already active" and changes nothing.
 */
void tiku_basic_mode_enter(void);

/**
 * @brief Run the saved program headlessly as a non-blocking mode (the
 *        `basic run` command).
 *
 * Loads the saved program, starts it and returns; the shell poll loop runs it
 * to completion with no prompt, then leaves the mode.
 *
 * @return 0 if a program started, else -1 (the reason is printed)
 */
int tiku_basic_mode_run_saved(void);

/**
 * @brief Resume the saved program from its checkpoint, or start it fresh when
 *        there is none, headlessly as a non-blocking mode: the
 *        power-failure-transparent autostart (`basic run resume`).
 *
 * A checkpoint is absent on a first boot and after an orderly end.
 *
 * @return 0 if a program is running, resumed or fresh, else -1
 */
int tiku_basic_mode_resume_saved(void);

/** @brief 1 while the shell is in BASIC mode (shell poll-loop hook). */
int tiku_basic_mode_active(void);

/**
 * @brief Feed one console byte to the mode (poll-loop hook).
 *
 * At the prompt it is the line editor: echo, backspace, CR/LF dispatches the
 * line and Ctrl-C leaves BASIC.  While a program runs only Ctrl-C (break) and,
 * with the debugger paused, a DEBUG command line act; other bytes are dropped.
 *
 * @note Ignored while the mode is off or a memory reclaim holds BASIC.
 */
void tiku_basic_mode_feed_char(int ch);

struct tiku_shell_io;

/**
 * @brief Drive the mode from @p io instead of the console, NULL to undo.
 *
 * The swap holds only inside the mode's hooks, so the shell keeps its own
 * console line and answers namespace reads while a program runs.
 */
void tiku_basic_mode_set_stream(const struct tiku_shell_io *io);

/** @brief Non-zero while the mode is driven from a stream, not the console. */
int tiku_basic_mode_streamed(void);

/**
 * @brief Run up to one batch (TIKU_BASIC_MODE_BATCH) of program steps
 *        (poll-loop hook).
 *
 * A no-op at the prompt.  When the program ends it returns to the prompt, or
 * leaves the mode after a headless run.
 */
void tiku_basic_mode_tick(void);

/**
 * @brief Notify BASIC that a watched VFS node changed (event-driven
 *        ON CHANGE).
 *
 * Marks every event-armed ON CHANGE slot on @p node pending; the RUN loop
 * re-reads it and fires its handler at the next line boundary.
 *
 * @note Call from the shell's TIKU_EVENT_VFS dispatch with the changed node
 *       pointer.  A no-op when ON CHANGE events are compiled out, no program
 *       is running or a memory reclaim holds BASIC.
 */
void tiku_basic_mode_on_vfs(const void *node);

/**
 * @brief Consume the "BASIC mode just exited" edge (shell poll-loop hook).
 * @return 1 once after the mode leaves, else 0; the shell then reprints its
 *         prompt.
 */
int tiku_basic_mode_take_exit(void);

/**
 * @brief Load the saved program and RUN it to completion (blocking).
 *
 * `basic run <path>` calls this after storing the file as the saved program.
 * Runs nothing if no program is saved or a BASIC session is live.
 */
void tiku_basic_autorun(void);

/**
 * @brief Parse a multi-line BASIC source string and RUN it.
 *
 * The build-time BASIC_PROGRAM=foo.bas path: the .bas file becomes a
 * NUL-terminated C string in the firmware, and main.c calls this at boot.
 * Numbered lines are stored, un-numbered ones execute as at the REPL.
 *
 * @note An implicit RUN fires after parsing unless the source already issued
 *       one.  Pair with tiku_shell_io_set_backend() so PRINT reaches the active
 *       transport.
 * @param source NUL-terminated source text; '\n' separates lines.
 */
void tiku_basic_run_source(const char *source);

/*---------------------------------------------------------------------------*/
/* VFS BRIDGE -- /data/basic file node                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the saved BASIC program text into @p buf.
 *
 * The read handler of the /data/basic VFS file node.
 *
 * @param buf  Destination buffer.
 * @param max  Capacity of @p buf in bytes.
 *
 * @return Number of bytes written: 0 when no program is saved or it does not
 *         fit @p max, -1 when @p buf is NULL or @p max is 0.
 */
int tiku_basic_vfs_read(char *buf, unsigned int max);

/**
 * @brief Write @p len bytes of program text into the saved-program slot
 *        (prog.bas, or the persist store on MSP430/host).
 *
 * The write handler of the /data/basic VFS file node; the text is stored
 * verbatim.  Each line should be a numbered BASIC statement separated by
 * '\n', as `LIST` emits.
 *
 * @param buf  Source buffer (numbered BASIC source text).
 * @param len  Number of bytes to write.
 *
 * @return 0 on success, -1 when @p buf is NULL, @p len exceeds
 *         TIKU_BASIC_SAVE_BUF_BYTES or the store fails.
 */
int tiku_basic_vfs_write(const char *buf, unsigned int len);

/**
 * @brief Error sink callback: receives every interpreter error.
 *
 * @param cat  Error category, one of TIKU_BASIC_ERR_* (tiku_basic_config.h).
 * @param msg  Bare message text (no color codes, no "? " prefix, no newline).
 */
typedef void (*tiku_basic_error_sink_t)(int cat, const char *msg);

/**
 * @brief Install a custom error sink so BASIC can run headless.
 *
 * By default interpreter errors print to the shell console as a red
 * "? message".  A sink receives them instead, for a build with no shell or
 * UART attached.
 *
 * @param sink  Callback to receive errors, or NULL for the default.
 */
void tiku_basic_set_error_sink(tiku_basic_error_sink_t sink);

#endif /* TIKU_BASIC_H_ */
