/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_rules.c - "rules" command implementation
 *
 * Lists active rules with format
 *   #ID  on PATH OP VALUE        -> ACTION
 * and supports `rules del <id>` to free a slot.  The condition field
 * (everything before "->") is left-padded to a fixed width so the
 * arrow column is stable for short rules and gracefully extends for
 * long ones.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_rules.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_rules.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Walk the rule table and print one row per active slot.
 */
static void
rules_list(void)
{
    const tiku_shell_rule_t *r;
    char cond[64];
    uint8_t pos;
    uint8_t i;
    uint8_t active = 0;
    const char *part;
    const char *op_str;

    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        r = tiku_shell_rules_get(i);
        if (r == (const tiku_shell_rule_t *)0) {
            continue;
        }
        active++;

        op_str = tiku_shell_rules_op_name(r->op);

        /* Two display layouts:
         *   - Comparison rules: "on PATH OP VALUE"
         *   - CHANGED rules:    "on changed PATH"  (value[] holds the
         *                       last seen reading, not a user-visible
         *                       constant, so it is omitted from the
         *                       summary)
         */
        pos = 0;
        cond[pos++] = 'o';
        cond[pos++] = 'n';
        cond[pos++] = ' ';

        if (r->op == TIKU_SHELL_RULE_OP_CHANGED) {
            part = "changed ";
            while (*part != '\0' && pos < (uint8_t)(sizeof(cond) - 1)) {
                cond[pos++] = *part++;
            }
            part = r->path;
            while (*part != '\0' && pos < (uint8_t)(sizeof(cond) - 1)) {
                cond[pos++] = *part++;
            }
        } else {
            part = r->path;
            while (*part != '\0' && pos < (uint8_t)(sizeof(cond) - 1)) {
                cond[pos++] = *part++;
            }
            if (pos < (uint8_t)(sizeof(cond) - 1)) {
                cond[pos++] = ' ';
            }
            part = op_str;
            while (*part != '\0' && pos < (uint8_t)(sizeof(cond) - 1)) {
                cond[pos++] = *part++;
            }
            if (pos < (uint8_t)(sizeof(cond) - 1)) {
                cond[pos++] = ' ';
            }
            part = r->value;
            while (*part != '\0' && pos < (uint8_t)(sizeof(cond) - 1)) {
                cond[pos++] = *part++;
            }
        }
        cond[pos] = '\0';

        SHELL_PRINTF("  #%u  %-25s -> %s\n",
                     (unsigned)i, cond, r->action);
    }

    if (active == 0) {
        SHELL_PRINTF("(no rules)\n");
    }
}

/**
 * @brief Parse a small unsigned decimal id.
 */
static uint8_t
rules_parse_id(const char *s, uint16_t *out)
{
    uint16_t val = 0;
    uint8_t i;

    if (s[0] == '\0') {
        return 0;
    }
    for (i = 0; s[i] != '\0'; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        if (val > (uint16_t)(65535U / 10U)) {
            return 0;
        }
        val = (uint16_t)(val * 10U + (uint16_t)(s[i] - '0'));
    }
    *out = val;
    return 1;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_rules(uint8_t argc, const char *argv[])
{
    if (argc == 1) {
        rules_list();
        return;
    }

    if (argc == 3 && strcmp(argv[1], "del") == 0) {
        uint16_t id;
        if (!rules_parse_id(argv[2], &id) || id >= TIKU_SHELL_RULES_MAX) {
            SHELL_PRINTF("rules: invalid id '%s'\n", argv[2]);
            return;
        }
        if (tiku_shell_rules_del((uint8_t)id) != 0) {
            SHELL_PRINTF("rules: no rule at #%u\n", (unsigned)id);
            return;
        }
        SHELL_PRINTF("Deleted rule #%u\n", (unsigned)id);
        return;
    }

    SHELL_PRINTF("Usage: rules              List active rules\n");
    SHELL_PRINTF("       rules del <id>     Delete rule by id\n");
}
