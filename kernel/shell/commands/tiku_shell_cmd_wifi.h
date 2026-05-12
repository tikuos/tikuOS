/*
 * Tiku Operating System
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

void tiku_shell_cmd_wifi(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_WIFI_H_ */
