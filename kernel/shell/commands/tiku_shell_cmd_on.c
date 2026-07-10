/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_on.c - "on" command implementation
 *
 * Thin wrapper over tiku_shell_rules_add_argv() that registers a
 * reactive rule.  All parsing, validation, and diagnostics live in
 * the rules subsystem.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_on.h"
#include <kernel/shell/tiku_shell_rules.h>

void
tiku_shell_cmd_on(uint8_t argc, const char *argv[])
{
    (void)tiku_shell_rules_add_argv(argc, argv);
}
