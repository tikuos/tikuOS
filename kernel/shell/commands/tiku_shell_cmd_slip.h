/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_slip.h - "slip" command: toggle the console's IPv4 channel
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_SLIP_H_
#define TIKU_SHELL_CMD_SLIP_H_

#include <stdint.h>

/**
 * @brief "slip" command: turn the console's IPv4 channel on or off.
 *
 * `slip on` registers the channel, `slip off` removes it, and a bare `slip`
 * toggles.  While it is on, IP frames and keystrokes share the console line.
 *
 * @param argc  Argument count (including the command name)
 * @param argv  Argument strings (argv[0] is the command name)
 */
void tiku_shell_cmd_slip(uint8_t argc, const char *argv[]);

/**
 * @brief Whether the console's IPv4 channel is registered.
 *
 * @return 1 if SLIP is on, 0 otherwise.
 */
uint8_t tiku_shell_cmd_slip_active(void);

/**
 * @brief Turn SLIP on (idempotent): install the SLIP link when no other link
 *        is set, and register the console's IPv4 channel.
 *
 * Net commands (ping, ntp, dns, mqtt, syslog) call it before they send
 * traffic.  SLIP stays on afterwards.
 */
void tiku_shell_cmd_slip_enable(void);

#endif /* TIKU_SHELL_CMD_SLIP_H_ */
