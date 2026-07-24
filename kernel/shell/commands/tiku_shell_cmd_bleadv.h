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
 * The bare form "bleadv <name> [secs]" starts a background demo beacon
 * that stops itself.  Sub-commands: "on <name> [ms]" and "off"
 * (background beacon), "scan [secs] [prefix]" and "observe [secs|off]"
 * (passive scanning), "ext <name> [secs]" (extended advertising),
 * "conn [secs]" and "connprobe [secs]" (peripheral role), "central",
 * "cenupd", "censmp", "cenbond" and "cenphy" [secs] (central role),
 * "phy", "phytx" and "phyrx" (multi-PHY probes), "csa1" and "ackfsm"
 * (link-layer self-tests) and "dbg" (silicon/clock/radio readbacks).
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] is the sub-command or the beacon
 *              name, argv[2..] carry its duration and name parameters
 */
void tiku_shell_cmd_bleadv(uint8_t argc, const char *argv[]);
#endif

#endif /* TIKU_SHELL_CMD_BLEADV_H_ */
