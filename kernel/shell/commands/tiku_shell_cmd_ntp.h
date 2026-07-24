/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ntp.h - "ntp" command: fetch wall-clock time over SLIP (SNTP)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_NTP_H_
#define TIKU_SHELL_CMD_NTP_H_

#include <stdint.h>

/**
 * @brief "ntp" command -- query an SNTP server for wall-clock time.
 *
 * Usage: ntp [a.b.c.d]
 *
 * Enables SLIP mode (so the shell's shared RX demux routes the UDP reply to
 * the IP stack), sends one SNTP request, and prints the received UTC time.
 * With no argument the request goes to the SLIP host (the device subnet's
 * .1 address).  Non-blocking: the reply is awaited across shell ticks via
 * tiku_shell_cmd_ntp_tick(), so the shell stays interactive.
 */
void tiku_shell_cmd_ntp(uint8_t argc, const char *argv[]);

/** @brief True while an NTP query is in flight (awaiting reply/timeout). */
uint8_t tiku_shell_cmd_ntp_active(void);

/** @brief Per-tick driver: polls for the reply, prints it, or times out. */
void tiku_shell_cmd_ntp_tick(void);

#endif /* TIKU_SHELL_CMD_NTP_H_ */
