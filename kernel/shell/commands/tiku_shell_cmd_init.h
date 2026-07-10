/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_init.h - "init" command: manage FRAM boot entries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_INIT_H_
#define TIKU_SHELL_CMD_INIT_H_

#include <stdint.h>

/**
 * @brief "init" command handler — manage FRAM-backed boot entries.
 *
 * Subcommands:
 *   init list                       — show all entries
 *   init add <seq> <name> <cmd...>  — add or replace an entry
 *   init rm <name>                  — remove an entry
 *   init enable <name>              — enable a disabled entry
 *   init disable <name>             — disable without removing
 *   init run                        — re-execute all entries now
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_init(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_INIT_H_ */
