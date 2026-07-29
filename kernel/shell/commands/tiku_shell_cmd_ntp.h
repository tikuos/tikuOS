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
 * Enables SLIP mode so the shell's RX demux routes the UDP reply to the IP
 * stack, sends one request (to the SLIP host by default) and prints the UTC
 * time.  Non-blocking: the reply is awaited across shell ticks.
 */
void tiku_shell_cmd_ntp(uint8_t argc, const char *argv[]);

/** @brief True while an NTP query is in flight (awaiting reply/timeout). */
uint8_t tiku_shell_cmd_ntp_active(void);

/** @brief Per-tick driver: polls for the reply, prints it, or times out. */
void tiku_shell_cmd_ntp_tick(void);

#endif /* TIKU_SHELL_CMD_NTP_H_ */
