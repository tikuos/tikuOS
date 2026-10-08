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
 * @brief "layout" command handler; with no subcommand it runs show.
 *
 * Subcommands: show, limits, status, plan, stage, cancel, resume, inspect and
 * recover.  A change is staged for the next boot; one that rewrites /data
 * while it holds files also needs --erase.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @note Defined only when TIKU_SHELL_CMD_LAYOUT is set; never on MSP430.
 */
void tiku_shell_cmd_layout(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_LAYOUT_H_ */
