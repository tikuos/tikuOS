/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ntp.h - "ntp" command: fetch wall-clock time over SLIP (SNTP)
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
