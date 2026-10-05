/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_bleadv.h - "bleadv": BLE beacon, scan and link tests (opt-in).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_BLEADV_H_
#define TIKU_SHELL_CMD_BLEADV_H_

#include <stdint.h>
#include <kernel/shell/tiku_shell_config.h>

#if TIKU_SHELL_CMD_BLEADV
/**
 * @brief "bleadv" command handler — BLE beacon, scan and link tests.
 *
 * `bleadv <name> [secs]` starts a self-stopping demo beacon.  Sub-commands
 * cover the background beacon, scanning, extended advertising, both link
 * roles, pairing and bonding, PHY probes, crypto self-tests and FLPR tests.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] is the sub-command or the beacon
 *              name, argv[2..] carry its parameters
 */
void tiku_shell_cmd_bleadv(uint8_t argc, const char *argv[]);
#endif

#endif /* TIKU_SHELL_CMD_BLEADV_H_ */
