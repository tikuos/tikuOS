/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_dns.h - "dns" command: resolve a hostname to an IPv4 address
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

#ifndef TIKU_SHELL_CMD_DNS_H_
#define TIKU_SHELL_CMD_DNS_H_

#include <stdint.h>

/**
 * @brief "dns" command -- resolve a hostname to an IPv4 address.
 *
 * Usage: dns <hostname> [resolver-ip]
 *
 * Sends an A-record query to a recursive resolver (default 8.8.8.8, reached
 * through the SLIP host's relay/NAT) and prints the resolved address + TTL.
 * Non-blocking: the reply is awaited across shell ticks, polled at ~1 Hz to
 * match the resolver's retry budget.
 */
void tiku_shell_cmd_dns(uint8_t argc, const char *argv[]);

/** @brief True while a DNS query is in flight (awaiting reply/timeout). */
uint8_t tiku_shell_cmd_dns_active(void);

/** @brief Per-tick driver: paced poll for the reply, prints it, or times out. */
void tiku_shell_cmd_dns_tick(void);

#endif /* TIKU_SHELL_CMD_DNS_H_ */
