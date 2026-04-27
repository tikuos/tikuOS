/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_rules.c - Reactive rule engine implementation
 *
 * Each rule stores a VFS path, a comparison operator, a right-hand
 * side, and an action command.  The tick reads the path, evaluates
 * the relation, and fires the action exactly when the match state
 * transitions from false to true.  Numeric comparison is preferred
 * for any operator; equality and inequality fall back to string
 * compare when either side is non-numeric, which is what makes
 * "/sys/power/source == battery" work the same way as
 * "/dev/temp0 > 40".
 *
 * Edge triggering keeps actions idempotent in the common pattern of
 * "on COND <set state>".  A rule that turns an LED on when temp
 * crosses a threshold fires once on the upward crossing; a paired
 * rule with the inverse condition fires once on the downward
 * crossing.  The kernel's VFS does not yet expose change-notify
 * subscribers, so evaluation is polling at shell-tick granularity
 * (~50 ms by default); the rule struct is laid out so a future
 * subscribe-based path can drive the same evaluator without
 * touching storage.
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

#include "tiku_shell_rules.h"
#include "tiku_shell.h"               /* SHELL_PRINTF */
#include "tiku_shell_parser.h"
#include <kernel/vfs/tiku_vfs.h>
#include <limits.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

static tiku_shell_rule_t rule_table[TIKU_SHELL_RULES_MAX];

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Strict signed-decimal parse with overflow guard.
 * @return 1 on success, 0 on parse error or overflow.
 */
static uint8_t
rules_parse_long(const char *s, long *out)
{
    long val = 0;
    uint8_t neg = 0;
    long digit;

    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if (*s == '\0') {
        return 0;
    }
    while (*s != '\0') {
        if (*s < '0' || *s > '9') {
            return 0;
        }
        digit = (long)(*s - '0');
        if (val > (LONG_MAX - digit) / 10) {
            return 0;
        }
        val = val * 10 + digit;
        s++;
    }
    *out = neg ? -val : val;
    return 1;
}

/**
 * @brief Copy a NUL-terminated string with a length cap.
 * @return 1 on success, 0 if @p src does not fit.
 */
static uint8_t
rules_copy_field(char *dst, uint8_t cap, const char *src)
{
    uint8_t i;

    for (i = 0; i < cap - 1; i++) {
        dst[i] = src[i];
        if (src[i] == '\0') {
            return 1;
        }
    }
    if (src[i] != '\0') {
        return 0;
    }
    dst[i] = '\0';
    return 1;
}

/**
 * @brief Parse a comparison operator token.
 * @return 1 on success, 0 if @p s is not one of the six known tokens.
 */
static uint8_t
rules_parse_op(const char *s, tiku_shell_rule_op_t *out)
{
    if (strcmp(s, ">")  == 0) { *out = TIKU_SHELL_RULE_OP_GT; return 1; }
    if (strcmp(s, "<")  == 0) { *out = TIKU_SHELL_RULE_OP_LT; return 1; }
    if (strcmp(s, ">=") == 0) { *out = TIKU_SHELL_RULE_OP_GE; return 1; }
    if (strcmp(s, "<=") == 0) { *out = TIKU_SHELL_RULE_OP_LE; return 1; }
    if (strcmp(s, "==") == 0) { *out = TIKU_SHELL_RULE_OP_EQ; return 1; }
    if (strcmp(s, "!=") == 0) { *out = TIKU_SHELL_RULE_OP_NE; return 1; }
    return 0;
}

/**
 * @brief Evaluate the relation @p lhs OP @p rhs.
 *
 * Numeric for ordering operators (false if either side won't parse).
 * Equality/inequality is numeric when both sides parse; falls back to
 * lexical strcmp otherwise so string-valued nodes compare cleanly.
 */
static uint8_t
rules_evaluate(const char *lhs, tiku_shell_rule_op_t op, const char *rhs)
{
    long la = 0;
    long lb = 0;
    uint8_t la_ok = rules_parse_long(lhs, &la);
    uint8_t lb_ok = rules_parse_long(rhs, &lb);

    switch (op) {
    case TIKU_SHELL_RULE_OP_GT:
        return (la_ok && lb_ok && la >  lb) ? 1 : 0;
    case TIKU_SHELL_RULE_OP_LT:
        return (la_ok && lb_ok && la <  lb) ? 1 : 0;
    case TIKU_SHELL_RULE_OP_GE:
        return (la_ok && lb_ok && la >= lb) ? 1 : 0;
    case TIKU_SHELL_RULE_OP_LE:
        return (la_ok && lb_ok && la <= lb) ? 1 : 0;
    case TIKU_SHELL_RULE_OP_EQ:
        if (la_ok && lb_ok) {
            return (la == lb) ? 1 : 0;
        }
        return (strcmp(lhs, rhs) == 0) ? 1 : 0;
    case TIKU_SHELL_RULE_OP_NE:
        if (la_ok && lb_ok) {
            return (la != lb) ? 1 : 0;
        }
        return (strcmp(lhs, rhs) != 0) ? 1 : 0;
    case TIKU_SHELL_RULE_OP_CHANGED:
        /* OP_CHANGED is handled in the tick before reaching this
         * helper; if it ever does, treat as "no match" defensively. */
        return 0;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

void
tiku_shell_rules_init(void)
{
    /* rule_table is zero-initialised in BSS (TIKU_SHELL_RULE_FREE == 0).
     * Hook kept for symmetry with other shell subsystems and as a place
     * to plug FRAM recovery in future. */
}

int8_t
tiku_shell_rules_add(const char *path, tiku_shell_rule_op_t op,
                      const char *value, const char *action)
{
    uint8_t i;
    tiku_shell_rule_t *slot;

    if (path == (const char *)0 || value == (const char *)0 ||
        action == (const char *)0) {
        return -1;
    }

    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        if (rule_table[i].state == TIKU_SHELL_RULE_FREE) {
            slot = &rule_table[i];
            if (!rules_copy_field(slot->path,
                                  TIKU_SHELL_RULES_PATH_MAX, path)) {
                return -1;
            }
            if (!rules_copy_field(slot->value,
                                  TIKU_SHELL_RULES_VALUE_MAX, value)) {
                return -1;
            }
            if (!rules_copy_field(slot->action,
                                  TIKU_SHELL_RULES_ACTION_MAX, action)) {
                return -1;
            }
            slot->op         = op;
            slot->last_match = 0;
            slot->state      = TIKU_SHELL_RULE_ACTIVE;   /* publish last */
            return (int8_t)i;
        }
    }
    return -1;
}

int8_t
tiku_shell_rules_del(uint8_t id)
{
    if (id >= TIKU_SHELL_RULES_MAX) {
        return -1;
    }
    if (rule_table[id].state == TIKU_SHELL_RULE_FREE) {
        return -1;
    }
    rule_table[id].state = TIKU_SHELL_RULE_FREE;
    return 0;
}

const tiku_shell_rule_t *
tiku_shell_rules_get(uint8_t id)
{
    if (id >= TIKU_SHELL_RULES_MAX) {
        return (const tiku_shell_rule_t *)0;
    }
    if (rule_table[id].state == TIKU_SHELL_RULE_FREE) {
        return (const tiku_shell_rule_t *)0;
    }
    return &rule_table[id];
}

const char *
tiku_shell_rules_op_name(tiku_shell_rule_op_t op)
{
    switch (op) {
    case TIKU_SHELL_RULE_OP_GT:      return ">";
    case TIKU_SHELL_RULE_OP_LT:      return "<";
    case TIKU_SHELL_RULE_OP_GE:      return ">=";
    case TIKU_SHELL_RULE_OP_LE:      return "<=";
    case TIKU_SHELL_RULE_OP_EQ:      return "==";
    case TIKU_SHELL_RULE_OP_NE:      return "!=";
    case TIKU_SHELL_RULE_OP_CHANGED: return "changed";
    }
    return "?";
}

/**
 * @brief Join argv tokens [start..argc-1] with single spaces into @p out.
 * @return 1 on success, 0 if the joined string would overflow.
 */
static uint8_t
rules_join_action(uint8_t argc, const char *argv[], uint8_t start,
                   char *out, uint8_t outsz)
{
    uint8_t pos = 0;
    uint8_t i;
    const char *t;

    for (i = start; i < argc; i++) {
        t = argv[i];
        if (i > start) {
            if (pos >= outsz - 1) {
                return 0;
            }
            out[pos++] = ' ';
        }
        while (*t != '\0') {
            if (pos >= outsz - 1) {
                return 0;
            }
            out[pos++] = *t++;
        }
    }
    out[pos] = '\0';
    return 1;
}

int8_t
tiku_shell_rules_add_argv(uint8_t argc, const char *argv[])
{
    char action[TIKU_SHELL_RULES_ACTION_MAX];
    tiku_shell_rule_op_t op;
    const char *path;
    const char *value;
    uint8_t action_start;
    int8_t id;

    if (argc < 4) {
        SHELL_PRINTF("Usage: on <path> <op> <value> <command...>\n");
        SHELL_PRINTF("       on changed <path> <command...>\n");
        SHELL_PRINTF("Ops: > < >= <= == !=\n");
        return -1;
    }

    /* Disambiguate the two grammars.  "on changed PATH ACTION..." sets
     * op = OP_CHANGED, path = argv[2], no RHS value (the value field
     * is repurposed by the tick to hold the last seen reading). */
    if (strcmp(argv[1], "changed") == 0) {
        if (argc < 4) {
            SHELL_PRINTF("Usage: on changed <path> <command...>\n");
            return -1;
        }
        op           = TIKU_SHELL_RULE_OP_CHANGED;
        path         = argv[2];
        value        = "";              /* baseline filled on first tick */
        action_start = 3;
    } else {
        if (argc < 5) {
            SHELL_PRINTF("Usage: on <path> <op> <value> <command...>\n");
            return -1;
        }
        if (!rules_parse_op(argv[2], &op)) {
            SHELL_PRINTF("on: unknown operator '%s' "
                         "(use > < >= <= == != or 'changed')\n", argv[2]);
            return -1;
        }
        path         = argv[1];
        value        = argv[3];
        action_start = 4;
    }

    if (!rules_join_action(argc, argv, action_start,
                            action, TIKU_SHELL_RULES_ACTION_MAX)) {
        SHELL_PRINTF("on: action too long\n");
        return -1;
    }

    id = tiku_shell_rules_add(path, op, value, action);
    if (id < 0) {
        SHELL_PRINTF("on: rule rejected (path/value too long, "
                     "or no free slots, max %u)\n",
                     (unsigned)TIKU_SHELL_RULES_MAX);
        return -1;
    }
    /* Success is silent; `rules` shows the new entry. */
    return id;
}

/**
 * @brief Copy r->action into actionbuf so the parser can tokenise in place.
 */
static void
rules_copy_action(char *actionbuf, const tiku_shell_rule_t *r)
{
    uint8_t j;
    for (j = 0; j < TIKU_SHELL_RULES_ACTION_MAX - 1; j++) {
        actionbuf[j] = r->action[j];
        if (r->action[j] == '\0') {
            break;
        }
    }
    actionbuf[TIKU_SHELL_RULES_ACTION_MAX - 1] = '\0';
}

void
tiku_shell_rules_tick(void)
{
    char readbuf[TIKU_SHELL_RULES_VALUE_MAX];
    char actionbuf[TIKU_SHELL_RULES_ACTION_MAX];
    uint8_t i;
    uint8_t k;
    int n;
    uint8_t fire;

    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        tiku_shell_rule_t *r = &rule_table[i];

        if (r->state != TIKU_SHELL_RULE_ACTIVE) {
            continue;
        }

        n = tiku_vfs_read(r->path, readbuf, sizeof(readbuf) - 1);
        if (n < 0) {
            /* Path missing or read failed: clear the per-rule
             * "remembered" state so the rule re-baselines (CHANGED) /
             * re-edges (comparison) the next time the path returns. */
            r->last_match = 0;
            continue;
        }
        readbuf[n] = '\0';
        while (n > 0 && (readbuf[n - 1] == '\n' ||
                         readbuf[n - 1] == '\r' ||
                         readbuf[n - 1] == ' ')) {
            readbuf[--n] = '\0';
        }

        if (r->op == TIKU_SHELL_RULE_OP_CHANGED) {
            /* CHANGED: value[] holds the last-seen reading.  First
             * tick after add (or after a read failure) just baselines
             * without firing; subsequent ticks fire whenever the
             * reading differs from the stored baseline, then update
             * the baseline. */
            if (r->last_match == 0) {
                for (k = 0; k < TIKU_SHELL_RULES_VALUE_MAX - 1; k++) {
                    r->value[k] = readbuf[k];
                    if (readbuf[k] == '\0') {
                        break;
                    }
                }
                r->value[TIKU_SHELL_RULES_VALUE_MAX - 1] = '\0';
                r->last_match = 1;
                continue;
            }
            if (strcmp(r->value, readbuf) == 0) {
                continue;
            }
            for (k = 0; k < TIKU_SHELL_RULES_VALUE_MAX - 1; k++) {
                r->value[k] = readbuf[k];
                if (readbuf[k] == '\0') {
                    break;
                }
            }
            r->value[TIKU_SHELL_RULES_VALUE_MAX - 1] = '\0';

            rules_copy_action(actionbuf, r);
            tiku_shell_parser_execute(actionbuf);
            continue;
        }

        /* Comparison ops: edge-triggered (false -> true). */
        fire = rules_evaluate(readbuf, r->op, r->value);

        if (fire && !r->last_match) {
            rules_copy_action(actionbuf, r);
            r->last_match = 1;
            tiku_shell_parser_execute(actionbuf);
        } else {
            r->last_match = fire;
        }
    }
}
