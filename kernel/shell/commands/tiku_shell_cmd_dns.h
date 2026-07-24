/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_dns.h - "dns" command: resolve a hostname to an IPv4 address
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
