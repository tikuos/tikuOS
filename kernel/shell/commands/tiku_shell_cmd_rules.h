/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_rules.h - "rules" command: list / delete reactive rules
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_RULES_H_
#define TIKU_SHELL_CMD_RULES_H_

#include <stdint.h>

/**
 * @brief "rules" command — manage reactive rules.
 *
 * Usage:
 *   rules              List all active rules
 *   rules del <id>     Delete the rule at @p id
 */
void tiku_shell_cmd_rules(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_RULES_H_ */
