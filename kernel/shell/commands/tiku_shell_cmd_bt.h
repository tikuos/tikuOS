/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_bt.h - "bt" shell command (CYW43439 Bluetooth)
 *
 * Compiled in only when both TIKU_DRV_WIFI_CYW43_ENABLE and
 * TIKU_DRV_WIFI_CYW43_BT_ENABLE are set; the table entry in
 * tiku_shell.c is gated identically.
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
 * @brief "bt" command handler — drive the CYW43439 Bluetooth subsystem.
 *
 * Sub-commands: "status" (BD_ADDR, HCI/LMP version, firmware, adv/scan
 * state), "advertise <name>|stop", "scan [stop]", "list" (cached scan
 * results), "connections", "connect <slot|addr> [public]",
 * "disconnect [N]", "discover [N]", "read <handle> [N]",
 * "subscribe <cccd_handle> [N]", "bonds", "unpair [N]" and "help".
 * With no argument it prints the help.
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
