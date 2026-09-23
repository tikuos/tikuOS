/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_layout.h - "layout" command interface.
 *
 * Shows and changes the memory budgets the layout service applies at boot.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_LAYOUT_H_
#define TIKU_SHELL_CMD_LAYOUT_H_

#include <stdint.h>

/**
 * @brief "layout" command handler.
 *
 * Subcommands: show, limits, plan, stage, status, cancel and resume.  A change
 * is staged for the next boot; one that rewrites /data needs --erase.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_layout(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_LAYOUT_H_ */
