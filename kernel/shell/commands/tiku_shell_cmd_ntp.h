/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ntp.h - "ntp" command: fetch wall-clock time over SNTP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_NTP_H_
#define TIKU_SHELL_CMD_NTP_H_

#include <stdint.h>

/**
 * @brief "ntp" command -- query an SNTP server for wall-clock time.
 *
 * Sends one request to a public server by default, or to the given IP or host
 * (resolved first), then prints the UTC time and sets the RTC.  Non-blocking:
 * the reply is awaited across shell ticks.
 *
 * @note Turns SLIP on (tiku_shell_cmd_slip_enable()) so the reply reaches the
 *       IP stack.
 */
void tiku_shell_cmd_ntp(uint8_t argc, const char *argv[]);

/** @brief True while an NTP query is in flight (awaiting reply/timeout). */
uint8_t tiku_shell_cmd_ntp_active(void);

/** @brief Per-tick driver: polls for the reply, prints it, or times out. */
void tiku_shell_cmd_ntp_tick(void);

#endif /* TIKU_SHELL_CMD_NTP_H_ */
