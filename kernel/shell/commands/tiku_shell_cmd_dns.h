/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_dns.h - "dns" command: resolve a hostname to IPv4
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_DNS_H_
#define TIKU_SHELL_CMD_DNS_H_

#include <stdint.h>

/**
 * @brief "dns" command -- resolve a hostname to an IPv4 address.
 *
 * Sends an A-record query to a recursive resolver (by default the configured
 * or DHCP-supplied one, else 8.8.8.8) and prints the address and TTL.
 * Non-blocking: the reply is awaited across shell ticks, polled at ~1 Hz.
 */
void tiku_shell_cmd_dns(uint8_t argc, const char *argv[]);

/** @brief True while a DNS query is in flight (awaiting reply/timeout). */
uint8_t tiku_shell_cmd_dns_active(void);

/** @brief Per-tick driver: polls for the reply, prints it, or times out. */
void tiku_shell_cmd_dns_tick(void);

#endif /* TIKU_SHELL_CMD_DNS_H_ */
