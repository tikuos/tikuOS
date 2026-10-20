/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_on.c - "on" command implementation
 *
 * Passes its arguments to tiku_shell_rules_add_argv(), which parses them,
 * registers the rule and prints any error.  With the LP agent, a rule whose
 * action starts with "wake" becomes an LP job instead.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_on.h"
#include <shell/tiku_shell_rules.h>
#include <shell/tiku_shell_config.h>
#if TIKU_SHELL_CMD_LP
#include "tiku_shell_cmd_lp.h"
#endif

void
tiku_shell_cmd_on(uint8_t argc, const char *argv[])
{
#if TIKU_SHELL_CMD_LP
    if (tiku_shell_lp_rule(argc, argv)) {
        return;
    }
#endif
    (void)tiku_shell_rules_add_argv(argc, argv);
}
