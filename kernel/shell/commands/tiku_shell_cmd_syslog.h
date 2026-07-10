/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_syslog.h - "syslog" command: send a remote log line (RFC 3164)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_SYSLOG_H_
#define TIKU_SHELL_CMD_SYSLOG_H_

#include <stdint.h>

/**
 * @brief "syslog" command -- send a remote syslog line over SLIP.
 *
 * Usage: syslog <message...>
 *
 * Sends one RFC 3164 datagram (UDP port 514) to the SLIP host (the device
 * subnet's .1 address) at severity INFO, facility LOCAL0.  Fire-and-forget:
 * syslog has no reply, so this completes synchronously and the shell prompt
 * returns immediately.
 */
void tiku_shell_cmd_syslog(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_SYSLOG_H_ */
