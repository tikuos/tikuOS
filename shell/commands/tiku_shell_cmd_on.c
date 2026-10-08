/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_on.c - "on" command implementation
 *
 * Passes its arguments to tiku_shell_rules_add_argv(), which parses them,
 * registers the rule and prints any error.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_on.h"
#include <shell/tiku_shell_rules.h>

void
tiku_shell_cmd_on(uint8_t argc, const char *argv[])
{
    (void)tiku_shell_rules_add_argv(argc, argv);
}
