/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_init.h - "init" command: manage the boot entries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_INIT_H_
#define TIKU_SHELL_CMD_INIT_H_

#include <stdint.h>

/**
 * @brief "init" command handler — manage the boot entries in the NVM config
 *        region.
 *
 * Sub-commands: list, add <seq> <name> <cmd...>, rm <name>, enable <name>,
 * disable <name>, and run (execute every enabled entry now).
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_init(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_INIT_H_ */
