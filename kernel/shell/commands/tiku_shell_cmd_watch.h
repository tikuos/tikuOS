/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_watch.h - "watch" command: live view of a VFS node
 *
 * Rebuilt on the watch primitive: writable nodes stream
 * event-driven (a print per write, zero work in between),
 * read-only nodes re-read on an interval.  The command is a
 * non-blocking shell-loop MODE — it returns immediately and the
 * shell stays interactive while values stream; Ctrl+C stops it.
 * The shell main loop drives the mode through the hooks below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_WATCH_H_
#define TIKU_SHELL_CMD_WATCH_H_

#include <stdint.h>

/**
 * @brief "watch" command — live view of a VFS node until Ctrl+C.
 *
 * Usage: watch <path> [interval]
 *
 *   path     Absolute or CWD-relative path to a readable FILE node
 *   interval Seconds between reads in INTERVAL mode (1..255,
 *            default 1); ignored in EVENT mode, where the display
 *            is change-driven
 *
 * Mode is chosen from the node: writable nodes subscribe via
 * tiku_vfs_watch() and print on every accepted write; read-only
 * (sensor) nodes re-read periodically.  Prints the current value
 * once, immediately, then returns — values stream asynchronously
 * while the shell remains fully interactive.  A second `watch`
 * replaces the running one.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_watch(uint8_t argc, const char *argv[]);

/*---------------------------------------------------------------------------*/
/* SHELL-LOOP MODE HOOKS                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Non-zero while a watch is streaming.
 *
 * The shell input path consults this to route keystrokes: Ctrl+C
 * cancels the watch, all other input is discarded (the modal feel
 * of the original blocking watch, without the blocking).
 */
uint8_t tiku_shell_cmd_watch_active(void);

/**
 * @brief Per-poll-tick service.
 *
 * INTERVAL mode: counts ticks, re-prints each elapsed interval.
 * EVENT mode: idempotent re-subscribe — self-heals the
 * subscription after the rules engine's wholesale
 * tiku_vfs_unwatch_all() re-arm drops it.
 */
void tiku_shell_cmd_watch_tick(void);

/**
 * @brief EVENT-mode dispatch; call on TIKU_EVENT_VFS.
 *
 * @param node_ptr  The changed node from the event's data payload
 */
void tiku_shell_cmd_watch_on_vfs(const void *node_ptr);

/**
 * @brief Stop the active watch (no-op when idle).
 *
 * Releases the EVENT-mode subscription.  Called by the shell's
 * Ctrl+C routing; also used internally on read failure and when a
 * new watch replaces a running one.
 */
void tiku_shell_cmd_watch_cancel(void);

#endif /* TIKU_SHELL_CMD_WATCH_H_ */
