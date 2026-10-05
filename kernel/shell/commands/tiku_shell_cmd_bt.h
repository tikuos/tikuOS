/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_bt.h - "bt" shell command (the BLE host stack)
 *
 * Built with a radio under the host stack: the CYW43439's BT extension
 * (TIKU_DRV_WIFI_CYW43_BT_ENABLE) or the ESP32-C61's controller
 * (TIKU_DRV_BLE_ESP_ENABLE), the same gate as its tiku_shell.c table entry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_BT_H_
#define TIKU_SHELL_CMD_BT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief "bt" command handler — drive the BLE host stack (CYW43439 BT or
 *        the ESP32-C61 controller).
 *
 * Sub-commands cover status, advertising, scanning and the cached results,
 * links (connect/disconnect/connections), GATT (discover/read/subscribe),
 * bonding, and power (on/off, ESP32-C61 only).  No argument prints the help.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] selects the sub-command, argv[2..]
 *              carry its name, slot, address or attribute handle
 */
void tiku_shell_cmd_bt(uint8_t argc, const char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_SHELL_CMD_BT_H_ */
