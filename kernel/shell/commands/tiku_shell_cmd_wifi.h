/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_wifi.h - "wifi" command: status, scan, ...
 *
 * Drives the Wi-Fi radio through tiku_wireless; built when a Wi-Fi driver
 * (CYW43 or ESP32-C61) is enabled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_WIFI_H_
#define TIKU_SHELL_CMD_WIFI_H_

#include <stdint.h>

/**
 * @brief "wifi" command handler — drive the Wi-Fi radio.
 *
 * Sub-commands: on, off, status, scan, list, connect (WPA2-PSK), connect3
 * (WPA3-SAE), disconnect, forget, help and, with TIKU_KITS_NET_WIFI_ENABLE,
 * up (IP over WiFi via DHCP).  No argument prints the usage summary.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] selects the sub-command, argv[2]
 *              and argv[3] carry the SSID and passphrase for the joins
 */
void tiku_shell_cmd_wifi(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_WIFI_H_ */
