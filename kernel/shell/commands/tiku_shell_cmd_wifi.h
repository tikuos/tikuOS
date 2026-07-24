/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_wifi.h - "wifi" command: status, scan, ...
 *
 * Drives the CYW43439 WiFi driver from the shell. Available only
 * when TIKU_DRV_WIFI_CYW43_ENABLE=1 is set.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_WIFI_H_
#define TIKU_SHELL_CMD_WIFI_H_

#include <stdint.h>

/**
 * @brief "wifi" command handler — drive the CYW43439 WiFi radio.
 *
 * Sub-commands: "status" (driver state, MAC, link), "scan" (start an
 * active scan), "list" (cached scan results), "connect <ssid> <psk>"
 * (WPA2-PSK join), "connect3 <ssid> <psk>" (WPA3-SAE join),
 * "disconnect", "forget" (clear stored credentials), "up" (bring the IP
 * stack up over WiFi via DHCP, when that kit is built in) and "help".
 * With no argument it prints the usage summary.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] selects the sub-command, argv[2]
 *              and argv[3] carry the SSID and passphrase for the joins
 */
void tiku_shell_cmd_wifi(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_WIFI_H_ */
