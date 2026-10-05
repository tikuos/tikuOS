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
 * Prints the address the net stack holds (TIKU_KITS_NET_IP_ADDR, or the one
 * DHCP assigned), the DHCP lease when there is one, and whether a host can
 * reach the address: on WiFi always, over SLIP only while SLIP is on.
 *
 * @param argc  Argument count (including the command name)
 * @param argv  Argument strings (argv[0] is the command name)
 */
void tiku_shell_cmd_ip(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_IP_H_ */
