/*
 * Tiku Operating System v0.05
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

void tiku_shell_cmd_bt(uint8_t argc, const char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_SHELL_CMD_BT_H_ */
