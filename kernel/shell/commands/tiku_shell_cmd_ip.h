/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ip.h - "ip" command: print the device's IPv4 address
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_IP_H_
#define TIKU_SHELL_CMD_IP_H_

#include <stdint.h>

/**
 * @brief "ip" command: print the device's IPv4 address.
 *
 *   ip
 *
 * Prints the device's current IPv4 address from the TikuKits net stack
 * (default 172.16.7.2 / TIKU_KITS_NET_IP_ADDR, or whatever DHCP assigned).
 * The address becomes reachable from the host once SLIP is on -- run
 * `slip` (or `ping`, which enables it).
 *
 * Requires the TikuKits net stack (TIKU_KIT_NET_ENABLE=1); compiled in
 * only when the stack is present.
 *
 * @param argc  Argument count (including the command name)
 * @param argv  Argument strings (argv[0] is the command name)
 */
void tiku_shell_cmd_ip(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_IP_H_ */
