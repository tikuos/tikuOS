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
 * @brief "dns" command: resolve a hostname to an IPv4 address.
 *
 * Usage: dns <hostname> [resolver-ip].  The resolver defaults to the
 * configured override, else the DHCP lease's, else 8.8.8.8.  The command
 * returns at once; tiku_shell_cmd_dns_tick() prints the address and TTL.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_dns(uint8_t argc, const char *argv[]);

/** @brief True while a DNS query is in flight (awaiting reply/timeout). */
uint8_t tiku_shell_cmd_dns_active(void);

/**
 * @brief Per-tick driver: polls for the reply, prints it, or times out.
 *
 * @note The shell poll loop calls it while tiku_shell_cmd_dns_active().
 */
void tiku_shell_cmd_dns_tick(void);

#endif /* TIKU_SHELL_CMD_DNS_H_ */
