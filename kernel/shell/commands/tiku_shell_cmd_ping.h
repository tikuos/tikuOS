/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ping.h - "ping" command: ICMP echo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_PING_H_
#define TIKU_SHELL_CMD_PING_H_

#include <stdint.h>

/**
 * @brief "ping" command: `ping <a.b.c.d> [count]`, ICMP echo a host.
 *
 * Turns SLIP on, then runs without blocking: each shell tick checks for the
 * reply the ICMP callback recorded and prints the round-trip time or a
 * timeout, ending with a summary after count probes (default 4).
 *
 * @note The run is bounded by count; Ctrl+C does not stop it.
 * @param argc  Argument count (including the command name)
 * @param argv  Argument strings (argv[0] is the command name)
 */
void tiku_shell_cmd_ping(uint8_t argc, const char *argv[]);

/**
 * @brief Whether a ping run is in progress.
 * @return 1 if ping mode is active, 0 otherwise.
 */
uint8_t tiku_shell_cmd_ping_active(void);

/**
 * @brief Per-tick service for the ping engine.
 *
 * Called once per shell poll tick while ping mode is active: matches the
 * reply the ICMP callback recorded, handles per-probe timeouts, and advances
 * to the next probe (or finishes the run).
 */
void tiku_shell_cmd_ping_tick(void);

#endif /* TIKU_SHELL_CMD_PING_H_ */
