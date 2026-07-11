/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_bleadv.h - nRF54L15 BLE beacon bring-up command (opt-in).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_BLEADV_H_
#define TIKU_SHELL_CMD_BLEADV_H_

#include <stdint.h>
#include <kernel/shell/tiku_shell_config.h>

#if TIKU_SHELL_CMD_BLEADV
void tiku_shell_cmd_bleadv(uint8_t argc, const char *argv[]);
#endif

#endif /* TIKU_SHELL_CMD_BLEADV_H_ */
