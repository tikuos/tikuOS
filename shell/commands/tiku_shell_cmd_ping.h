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
 * @brief Per-tick service for a ping run; does nothing when none is active.
 *
 * Prints the reply to the current probe or its timeout, then sends the next
 * probe, or prints the summary and ends the run.
 *
 * @note The shell loop calls it once per poll tick.
 */
void tiku_shell_cmd_ping_tick(void);

#endif /* TIKU_SHELL_CMD_PING_H_ */
