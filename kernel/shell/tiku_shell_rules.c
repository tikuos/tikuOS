/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_rules.c - reactive rule engine.
 *
 * A rule is a VFS path, an operator, a right-hand side and an action line.  The
 * tick fires the action only on a false-to-true transition, which keeps the common
 * "on COND set-state" pattern idempotent.  Comparison is numeric where it can be.
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
 * @brief Fixed-size table of rule slots -- the whole state of the engine.
 *
 * SRAM-only today, so rules are lost across reset; the struct layout leaves
 * room for a later FRAM-backed migration.  Lives in BSS, so every slot starts
 * zeroed and reads as TIKU_SHELL_RULE_FREE (== 0) with no init pass.
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
 * @brief Strict signed-decimal parse with overflow guard.
 *
 * An optional sign then one or more digits, and nothing else -- no whitespace
 * skip, no trailing-garbage tolerance, no partial parse.  That strictness is
 * what lets the evaluator decide cleanly whether a reading is numeric.
 *
 * @note Overflow is caught before it happens, so a value too large for a long
 *       is rejected rather than wrapping.  @p out is written only on success.
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
 * @brief Copy a NUL-terminated string into a fixed field with a cap.
 *
 * Writes at most @p cap-1 characters plus the NUL, so @p dst is always
 * terminated on success.  Loads the path, value and action fields of a slot
 * while enforcing the per-field maxima.
 *
 * @note Failure (a source longer than the field) returns 0 without guaranteeing
 *       termination; this is what surfaces as "path/value too long".
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
 * matching tiku_shell_rule_op_t.  @p out is written only on a match.
 *
 * @note OP_CHANGED is deliberately NOT parsed here: "changed" uses a different
 *       grammar and is detected before this helper is reached.
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
 * Both sides go through rules_parse_long() first.  Ordering operators are
 * strictly numeric and false unless BOTH sides parse; equality compares
 * numerically when both parse and falls back to strcmp otherwise.
 *
 * @note That dual mode is what makes "/sys/power/source == battery" compare
 *       text while "/dev/temp0 > 40" compares magnitude.  OP_CHANGED and any
 *       unknown enum yield 0 defensively.
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
        /* OP_CHANGED is handled in the tick before reaching this
         * helper; if it ever does, treat as "no match" defensively. */
        return 0;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the rule engine.
 *
 * Called once at shell startup.  rule_table lives in BSS and a free slot is
 * TIKU_SHELL_RULE_FREE (== 0), so the table is already empty and there is
 * nothing to clear; the hook is kept for symmetry and future FRAM recovery.
 */
/*
 * Re-derive every rule's trigger path from current state.
 *
 * The watch-subscription strategy is wholesale: drop every watch the shell
 * process holds, then walk the table and re-subscribe each ACTIVE rule whose
 * path resolves to a writable node.  tiku_vfs_watch() is idempotent and
 * unwatch_all() is one call, so this beats tracking which subscription belonged
 * to which possibly-just-deleted rule.
 *
 * Side effect on every ACTIVE rule: r->node is (re)cached.
 *   - node with a write handler  -> event-armed: watched, and the poll tick
 *     skips it (tiku_shell_rules_on_vfs() evaluates it)
 *   - node without write handler -> sensor-side: stays on the poll tick, since
 *     its value changes without writes and no event fires
 *   - unresolvable path          -> node = NULL: stays on the poll tick, which
 *     retries the read each pass so a path that appears later still works
 *
 * Called from init, and after every successful add / del / clear.
 */
static void
rules_rearm(void)
{
    uint8_t i;

    tiku_vfs_unwatch_all(&tiku_shell_process);

    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        tiku_shell_rule_t *r = &rule_table[i];

        if (r->state != TIKU_SHELL_RULE_ACTIVE) {
            r->node = (const tiku_vfs_node_t *)0;
            continue;
        }
        r->node = tiku_vfs_resolve(r->path);
        if (r->node != (const tiku_vfs_node_t *)0 &&
            r->node->write != (tiku_vfs_write_fn)0) {
            (void)tiku_vfs_watch(r->path, &tiku_shell_process);
        }
    }
}

void
tiku_shell_rules_init(void)
{
    /* rule_table is zero-initialised in BSS (TIKU_SHELL_RULE_FREE == 0).
     * The re-arm is a no-op on an empty table but establishes the
     * invariant that node caches and watch subscriptions always
     * reflect the table from here on. */
    rules_rearm();
}

/**
 * @brief Register a rule in the first free slot.
 *
 * Claims the lowest-index free slot and copies the path, value and action into
 * its fixed fields, failing if any overflows.  The state field is written LAST,
 * so the tick never observes a half-initialised rule.
 *
 * @note last_match is zeroed so the rule starts un-edged: a comparison already
 *       true on the first tick still fires once, and a CHANGED rule baselines
 *       without firing.
 * @param path    VFS path to read each tick (must fit PATH_MAX-1)
 * @param op      Comparison operator (or OP_CHANGED)
 * @param value   Right-hand side, or "" for OP_CHANGED (must fit VALUE_MAX-1)
 * @param action  Command line dispatched on a false->true transition
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
 * Sets the slot's state to TIKU_SHELL_RULE_FREE so the next add can reuse it.
 * The string fields are not wiped -- they are dead once free and overwritten on
 * the next add -- making this an O(1) state flip.
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
 * Walks the table and flips each non-free slot back to TIKU_SHELL_RULE_FREE.
 * Like the single-slot delete it only touches the state field, so it is cheap
 * regardless of how many rules were registered.
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
 * Reassembles the action portion of an "on" line for storage and later
 * re-tokenising.  One space between tokens, never leading or trailing, so
 * "led on 0" round-trips intact.
 *
 * @note Any run of whitespace the user typed collapses to a single space, which
 *       is harmless for dispatch.  The bound is checked before every byte, so
 *       @p out is never overrun and is always terminated on success.
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
 * The full front end for "on": accepts "on <path> <op> <value> <command...>"
 * (at least 5 arguments) and "on changed <path> <command...>", joins the
 * trailing tokens into an action and hands it to tiku_shell_rules_add().
 *
 * @note In the change grammar the stored value starts empty, because the tick
 *       repurposes that field to hold the last-seen reading.  Every failure
 *       prints a targeted line and returns -1; success is silent.
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
 *
 * tiku_shell_parser_execute() tokenises destructively, so a rule's action must
 * never be passed to it directly -- that would corrupt the stored rule.  The
 * copy stops at the source NUL and force-terminates the destination.
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
 * Trigger paths: poll tick vs. watch event.
 *
 * Two ways a rule gets evaluated, sharing one evaluator (rules_eval_one) so the
 * semantics are identical:
 *
 *   - POLL (tiku_shell_rules_tick, once per shell tick): sensor-side rules --
 *     nodes without a write handler, whose values change in the world rather
 *     than through tiku_vfs_write() -- plus rules whose path did not resolve at
 *     arm time, retried each pass.  Event-armed rules are skipped entirely, so
 *     their per-tick cost, including side-effectful reads like ADC
 *     conversions, is gone.
 *
 *   - EVENT (tiku_shell_rules_on_vfs, on TIKU_EVENT_VFS): rules whose node is
 *     writable.  Every successful write posts the event and the matching rules
 *     evaluate immediately, so write-to-reaction latency is one dispatch rather
 *     than up to a full poll period, and a value that pulses between ticks can
 *     no longer be missed.
 *
 * Evaluation (both paths): read the path into a stack buffer, strip the
 * trailing '\n'/'\r'/' ' run, and on a read failure clear last_match so the
 * rule re-baselines when the path returns.  OP_CHANGED baselines on first
 * evaluation then fires on any difference; comparison ops fire only on a
 * false->true edge.  A firing rule dispatches its action synchronously through
 * a scratch copy, exactly as if typed at the prompt.
 *
 * Loop note: an action that writes its own watched node re-enters through a
 * fresh event rather than waiting a tick.  Edge semantics still bound it, but
 * an action that alternates its own trigger value oscillates at event speed
 * instead of tick speed -- the same user error as before, faster.
 */
/**
 * @brief Evaluate one rule.
 *
 * Shared by the poll tick and the event path so both have byte-identical
 * semantics: read the path, strip the trailing newline run, baseline-or-compare
 * for CHANGED, edge-detect for comparison ops, dispatch through a scratch copy.
 *
 * @note The ~96 bytes of read/action buffers live on the caller's stack only
 *       for the duration of the call.
 * @param r  An ACTIVE rule slot
 */
static void
rules_eval_one(tiku_shell_rule_t *r)
{
    char readbuf[TIKU_SHELL_RULES_VALUE_MAX];
    char actionbuf[TIKU_SHELL_RULES_ACTION_MAX];
    uint8_t k;
    int n;
    uint8_t fire;

    /* By-node read whenever the path resolved at arm time — every
     * event-armed rule, and every resolvable sensor rule — which
     * skips the tree walk on the reactive hot path.  r->node == NULL
     * means the path did not resolve at arm: fall back to a by-path
     * read so the resolution is retried, the pre-watch behaviour for
     * paths that only appear later. */
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
    readbuf[n] = '\0';
    while (n > 0 && (readbuf[n - 1] == '\n' ||
                     readbuf[n - 1] == '\r' ||
                     readbuf[n - 1] == ' ')) {
        readbuf[--n] = '\0';
    }

    if (r->op == TIKU_SHELL_RULE_OP_CHANGED) {
        /* CHANGED: value[] holds the last-seen reading.  First
         * evaluation after add (or after a read failure) just
         * baselines without firing; subsequent evaluations fire
         * whenever the reading differs from the stored baseline,
         * then update the baseline. */
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
         * by tiku_shell_rules_on_vfs() the moment a write lands;
         * the poll path carries only sensor-side rules and paths
         * that did not resolve at arm time. */
        if (r->node != (const tiku_vfs_node_t *)0 &&
            r->node->write != (tiku_vfs_write_fn)0) {
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
