/*
 * Tiku Operating System v0.06
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
/**
 * @brief "bleadv" command handler — BLE beacon, scan and link harness.
 *
 * The bare form starts a self-stopping demo beacon.  Sub-commands cover the
 * background beacon (on/off), scanning (scan/observe), extended advertising,
 * both roles (conn/central and their variants), the PHY probes and link tests.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] is the sub-command or the beacon
 *              name, argv[2..] carry its duration and name parameters
 */
void tiku_shell_cmd_bleadv(uint8_t argc, const char *argv[]);
#endif

#endif /* TIKU_SHELL_CMD_BLEADV_H_ */
