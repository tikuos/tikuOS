/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_rules.c - reactive rule engine.
 *
 * A rule is a VFS path, an operator, a right-hand side and an action line.  A
 * comparison fires on a false-to-true transition, so its action runs once per
 * crossing, and compares as integers when both sides parse as integers.
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

/**
 * @brief Fixed-size table of rule slots, the whole state of the engine.
 *
 * Cleared at boot with .bss: rules are lost at reset, and every slot starts
 * as TIKU_SHELL_RULE_FREE (0).
 */
static tiku_shell_rule_t rule_table[TIKU_SHELL_RULES_MAX];

/**
 * @brief The shell process, which owns rule evaluation.
 *
 * Actions dispatch through the parser in shell context and TIKU_EVENT_VFS
 * notifications for event-armed rules are delivered to it.  Defined by
 * TIKU_PROCESS() in tiku_shell.c.
 */
extern struct tiku_process tiku_shell_process;

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Parse a signed decimal integer that fills the whole string.
 *
 * Accepts an optional sign and one or more digits, and nothing else: no
 * spaces, no trailing text.  A value past LONG_MAX fails.  @p out is written
 * only on success.
 *
 * @param s    NUL-terminated candidate string (caller guarantees non-NULL)
 * @param out  Receives the parsed value on success; untouched on failure
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
 * @brief Copy a NUL-terminated string into a fixed-size field.
 *
 * Writes at most @p cap - 1 characters and a NUL.  A source that does not fit
 * returns 0 and leaves @p dst unterminated.
 *
 * @param dst  Destination field
 * @param cap  Capacity of @p dst in bytes, including the NUL slot
 * @param src  NUL-terminated source string
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
 *
 * Maps the six textual operators (">", "<", ">=", "<=", "==", "!=") to the
 * matching tiku_shell_rule_op_t; @p out is written only on a match.
 * tiku_shell_rules_add_argv() handles "changed" before calling this.
 *
 * @param s    Operator token (caller guarantees non-NULL)
 * @param out  Receives the matching operator enum on success
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
 * Ordering operators compare integers and are false unless both sides parse;
 * == and != compare integers when both parse and text otherwise.  OP_CHANGED
 * and an unknown operator return 0.
 *
 * @param lhs  Left-hand value (the stripped VFS reading)
 * @param op   Comparison operator
 * @param rhs  Right-hand value (the rule's stored value field)
 * @return 1 if the relation holds, 0 otherwise.
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
        /* rules_eval_one() handles OP_CHANGED before calling this. */
        return 0;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Re-derive every rule's trigger path from current state.
 *
 * Drops every watch the shell process holds, then re-caches each active rule's
 * node and re-subscribes the writable ones; a refused watch leaves its rule on
 * the poll tick.  Called from init and after each successful add, del, clear.
 */
static void
rules_rearm(void)
{
    uint8_t i;

    /* The cached node decides an active rule's path:
     *   - a node with a write handler is event-armed: watched, evaluated by
     *     tiku_shell_rules_on_vfs() and skipped by the poll tick.  When the
     *     watch table is full the watch is refused, and the rule stays on
     *     the poll tick;
     *   - a node without one is sensor-side and stays on the poll tick,
     *     since its value changes without writes and no event fires;
     *   - an unresolved path leaves node NULL and stays on the poll tick,
     *     which retries the read each pass, so a path that appears later
     *     still works. */
    tiku_vfs_unwatch_all(&tiku_shell_process);

    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        tiku_shell_rule_t *r = &rule_table[i];

        r->armed = 0;
        if (r->state != TIKU_SHELL_RULE_ACTIVE) {
            r->node = (const tiku_vfs_node_t *)0;
            continue;
        }
        r->node = tiku_vfs_resolve(r->path);
        if (r->node != (const tiku_vfs_node_t *)0 &&
            r->node->write != (tiku_vfs_write_fn)0 &&
            tiku_vfs_watch(r->path, &tiku_shell_process) >= 0) {
            r->armed = 1;
        }
    }
}

/**
 * @brief Initialise the rule engine.
 *
 * rule_table starts empty in BSS (TIKU_SHELL_RULE_FREE is 0).  The re-arm
 * drops every watch the shell process holds, so from here on the node caches
 * and watch subscriptions match the table.
 */
void
tiku_shell_rules_init(void)
{
    rules_rearm();
}

/**
 * @brief Register a rule in the first free slot.
 *
 * Fills the lowest free slot and sets its state last, so a field that does
 * not fit leaves the slot free.  A comparison already true fires on its first
 * evaluation; a CHANGED rule's first evaluation records a baseline.
 *
 * @param path    VFS path the rule reads (must fit PATH_MAX-1)
 * @param op      Comparison operator (or OP_CHANGED)
 * @param value   Right-hand side, or "" for OP_CHANGED (must fit VALUE_MAX-1)
 * @param action  Command line dispatched each time the rule fires
 *                (must fit ACTION_MAX-1)
 * @return Slot id (>= 0) on success, -1 if a pointer is NULL, the table
 *         is full, or a field overflows its buffer.
 */
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
            rules_rearm();   /* cache the node; watch it if writable */
            return (int8_t)i;
        }
    }
    return -1;
}

/**
 * @brief Free a rule slot by id.
 *
 * Sets the slot's state to TIKU_SHELL_RULE_FREE so the next add can reuse it,
 * then re-arms the watches for the remaining rules.  The string fields are not
 * wiped: they are dead once free and overwritten on the next add.
 *
 * @param id  Slot id previously returned by add
 * @return 0 on success, -1 if @p id is out of range or already free.
 */
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
    rules_rearm();   /* drop the watch unless another rule shares it */
    return 0;
}

/**
 * @brief Free every active rule slot.
 *
 * Walks the table and flips each non-free slot back to TIKU_SHELL_RULE_FREE,
 * then re-arms, which releases every watch the shell process holds.
 *
 * @return Number of slots that were active and have been freed.
 */
uint8_t
tiku_shell_rules_clear(void)
{
    uint8_t n = 0;
    uint8_t i;

    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        if (rule_table[i].state != TIKU_SHELL_RULE_FREE) {
            rule_table[i].state = TIKU_SHELL_RULE_FREE;
            n++;
        }
    }
    rules_rearm();   /* releases every watch subscription */
    return n;
}

/**
 * @brief Read-only inspection of a rule slot.
 *
 * Returns a const pointer into rule_table so the "rules" command can render the
 * path, operator, value and action.  It aliases live engine storage, so callers
 * must not retain it across a delete or clear that could free the slot.
 *
 * @param id  Slot id to inspect
 * @return Pointer to the rule, or NULL if the slot is free or @p id is
 *         out of range.
 */
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

/**
 * @brief Convert an operator enum to its printable token.
 *
 * The inverse of rules_parse_op(), extended to cover OP_CHANGED; anything
 * outside the known enumerators renders as "?".  The returned pointer is a
 * string literal with static lifetime.
 *
 * @param op  Operator enum to name
 * @return Static printable token; never NULL.
 */
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
 *
 * Quotes the parser removed are not restored, so a quoted token holding
 * spaces runs as several tokens.  A result that does not fit returns 0
 * without writing past @p outsz.
 *
 * @param argc   Argument count from the parser
 * @param argv   Argument vector from the parser
 * @param start  Index of the first action token to include
 * @param out    Destination buffer for the joined action
 * @param outsz  Capacity of @p out in bytes, including the NUL slot
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

/**
 * @brief Parse, validate, and register a rule from "on" command argv.
 *
 * Accepts "on <path> <op> <value> <command...>" and "on changed <path>
 * <command...>", whose value starts empty and later holds the last reading.
 * Every failure prints one line and returns -1; success prints nothing.
 *
 * @param argc  Argument count as produced by the shell parser
 * @param argv  Argument vector; argv[0] is the "on" command name
 * @return Slot id (>= 0) on success, -1 on any error (message printed).
 */
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
     * op = OP_CHANGED, path = argv[2] and no RHS value; the value field
     * holds the last reading. */
    if (strcmp(argv[1], "changed") == 0) {
        if (argc < 4) {
            SHELL_PRINTF("Usage: on changed <path> <command...>\n");
            return -1;
        }
        op           = TIKU_SHELL_RULE_OP_CHANGED;
        path         = argv[2];
        value        = "";              /* first evaluation stores it */
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
 * @brief Copy r->action into actionbuf, which the parser may then tokenise.
 *
 * tiku_shell_parser_execute() writes NULs into the line it runs; given
 * r->action itself, it would cut the stored action at its first space.
 *
 * @param actionbuf  Destination scratch buffer (ACTION_MAX bytes)
 * @param r          Rule whose action is to be copied
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

/*
 * A rule is evaluated on one of two paths, both through rules_eval_one():
 *
 *   - poll (tiku_shell_rules_tick(), each shell poll): rules on nodes without
 *     a write handler, whose values change without tiku_vfs_write(); rules
 *     whose path did not resolve, retried each pass; and rules whose watch
 *     the full watch table refused.
 *
 *   - event (tiku_shell_rules_on_vfs(), on TIKU_EVENT_VFS): rules on a
 *     writable node the shell process watches.  Every successful write posts
 *     the event, so a value written and changed back between polls is seen.
 *
 * An action that writes its own rule's node is evaluated again on the next
 * event dispatch: a `changed` rule whose action changes the value fires on
 * every dispatch until the rule is deleted.
 */
/**
 * @brief Evaluate one active rule and run its action if it fires.
 *
 * Reads the node, cut to VALUE_MAX - 1 bytes, and strips trailing newlines and
 * spaces; a failed read clears last_match.  A CHANGED rule fires when the
 * reading differs from the stored one, a comparison on a false-to-true edge.
 *
 * @param r  An active rule slot
 */
static void
rules_eval_one(tiku_shell_rule_t *r)
{
    char readbuf[TIKU_SHELL_RULES_VALUE_MAX];
    char actionbuf[TIKU_SHELL_RULES_ACTION_MAX];
    uint8_t k;
    int n;
    uint8_t fire;

    /* A rule whose path resolved at arm time reads its cached node, with no
     * tree walk.  r->node == NULL means it did not resolve: the read goes by
     * path, which resolves it again on every pass. */
    if (r->node != NULL) {
        n = tiku_vfs_read_node(r->node, readbuf, sizeof(readbuf) - 1);
    } else {
        n = tiku_vfs_read(r->path, readbuf, sizeof(readbuf) - 1);
    }
    if (n < 0) {
        /* Path missing or read failed: clear the per-rule
         * "remembered" state so the rule re-baselines (CHANGED) /
         * re-edges (comparison) the next time the path returns. */
        r->last_match = 0;
        return;
    }
    if (n > (int)sizeof(readbuf) - 1) {     /* keep the NUL inside the buffer */
        n = (int)sizeof(readbuf) - 1;
    }
    readbuf[n] = '\0';
    while (n > 0 && (readbuf[n - 1] == '\n' ||
                     readbuf[n - 1] == '\r' ||
                     readbuf[n - 1] == ' ')) {
        readbuf[--n] = '\0';
    }

    if (r->op == TIKU_SHELL_RULE_OP_CHANGED) {
        /* CHANGED: value[] holds the last-seen reading.  The first
         * evaluation after an add or a failed read stores the reading
         * without firing; a later one that differs stores the new
         * reading and then runs the action. */
        if (r->last_match == 0) {
            for (k = 0; k < TIKU_SHELL_RULES_VALUE_MAX - 1; k++) {
                r->value[k] = readbuf[k];
                if (readbuf[k] == '\0') {
                    break;
                }
            }
            r->value[TIKU_SHELL_RULES_VALUE_MAX - 1] = '\0';
            r->last_match = 1;
            return;
        }
        if (strcmp(r->value, readbuf) == 0) {
            return;
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
        return;
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

void
tiku_shell_rules_tick(void)
{
    uint8_t i;

    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        tiku_shell_rule_t *r = &rule_table[i];

        if (r->state != TIKU_SHELL_RULE_ACTIVE) {
            continue;
        }

        /* Event-armed rules (writable node, watched) are evaluated
         * by tiku_shell_rules_on_vfs() on the event each write posts;
         * the poll path carries every other rule. */
        if (r->armed) {
            continue;
        }

        rules_eval_one(r);
    }
}

void
tiku_shell_rules_on_vfs(const void *node_ptr)
{
    uint8_t i;

    if (node_ptr == (const void *)0) {
        return;
    }

    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        tiku_shell_rule_t *r = &rule_table[i];

        if (r->state != TIKU_SHELL_RULE_ACTIVE) {
            continue;
        }
        if ((const void *)r->node != node_ptr) {
            continue;
        }
        rules_eval_one(r);
    }
}
