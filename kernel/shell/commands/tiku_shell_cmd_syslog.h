/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_syslog.h - "syslog" command: send a remote log line (RFC 3164)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
