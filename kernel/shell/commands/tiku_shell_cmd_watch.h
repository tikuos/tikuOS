/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_watch.h - "watch" command: live view of a VFS node.
 *
 * Writable nodes stream event-driven, read-only nodes re-read on an interval.
 * The command returns immediately and the shell main loop drives the mode through
 * the hooks below, so the shell stays interactive.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_WATCH_H_
#define TIKU_SHELL_CMD_WATCH_H_

#include <stdint.h>

/**
 * @brief "watch" command — live view of a VFS node until Ctrl+C.
 *
 * Mode comes from the node: a writable node subscribes via tiku_vfs_watch()
 * and prints on every accepted write, a read-only sensor node re-reads every
 * interval seconds (1..255, default 1).  A second `watch` replaces the first.
 *
 * @note Prints the current value once immediately and returns; values then
 *       stream asynchronously while the shell stays fully interactive.
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
 * INTERVAL mode counts ticks and re-prints each elapsed interval.  EVENT mode
 * re-subscribes idempotently, self-healing after the rules engine's wholesale
 * tiku_vfs_unwatch_all() re-arm drops the subscription.
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
