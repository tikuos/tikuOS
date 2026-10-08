/*
 * Tiku Operating System v0.06
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
 * @brief "syslog" command -- send a remote syslog line over UDP.
 *
 * Joins the arguments into one message and sends it as an RFC 3164 datagram
 * (UDP port 514), severity INFO, facility LOCAL0, to the .1 address of the
 * device's subnet (the SLIP host), and returns once it is sent.
 */
void tiku_shell_cmd_syslog(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_SYSLOG_H_ */
