/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_rules.c - Reactive rule engine implementation
 *
 * This file is the storage and evaluation core behind the shell's
 * "on" and "rules" commands.  A rule is a stored quadruple kept in a
 * fixed-size SRAM slot table (rule_table): a VFS path, a comparison
 * operator, a right-hand-side value, and an action command line.  The
 * tick reads the path, evaluates the relation, and fires the action
 * exactly when the match state transitions from false to true.
 * Numeric comparison is preferred for any operator; equality and
 * inequality fall back to string compare when either side is
 * non-numeric, which is what makes "/sys/power/source == battery"
 * work the same way as "/dev/temp0 > 40".
 *
 * Slot model: rule_table[] holds TIKU_SHELL_RULES_MAX slots, each a
 * tiku_shell_rule_t.  A slot is free when its state field is
 * TIKU_SHELL_RULE_FREE (numerically 0, which is the BSS-zero value),
 * so the table needs no explicit clear at boot.  tiku_shell_rules_add()
 * fills the first free slot and publishes it by writing the state
 * field last; the slot id (its index) is the handle returned to
 * callers and shown by the "rules" command.  Field semantics depend
 * on the operator: for the six comparison operators the value field
 * is the immutable right-hand side and last_match remembers the
 * previous tick's boolean result; for the OP_CHANGED pseudo-operator
 * the value field instead holds the last-seen reading and last_match
 * doubles as a "baseline established" flag.
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
 * Scheduling and scope: tiku_shell_rules_tick() is driven from the
 * shell main loop (gated on TIKU_SHELL_CMD_RULES), after the input
 * drain and the scheduled-job tick, so user keystrokes and "every"
 * jobs run first.  Fired actions are dispatched synchronously through
 * tiku_shell_parser_execute(), i.e. they run as if typed at the
 * prompt.  This engine backs only "on"/"rules"; the sibling "if"
 * (one-shot conditional), "every" (the tiku_shell_jobs scheduler) and
 * "watch" (interactive periodic read) commands are independent and do
 * not store state here.
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

/**
 * Fixed-size table of rule slots — the entire persistent-in-RAM
 * state of the engine.
 *
 * Storage is SRAM-only today (no .persistent attribute), so rules are
 * lost across reset; the struct layout deliberately leaves room for a
 * later FRAM-backed migration with a magic-number ring like the
 * history subsystem.  Lives in BSS, so every slot starts zeroed and
 * therefore reads as TIKU_SHELL_RULE_FREE (== 0) before any rule is
 * added — no init pass is required.  Indexed by slot id throughout
 * this module; TIKU_SHELL_RULES_MAX caps the number of concurrent
 * rules.
 */
static tiku_shell_rule_t rule_table[TIKU_SHELL_RULES_MAX];

/**
 * The shell process owns rule evaluation: actions dispatch through
 * the parser in shell context, and TIKU_EVENT_VFS notifications for
 * event-armed rules are delivered to it.  Defined by TIKU_PROCESS()
 * in tiku_shell.c; an extern object declaration against the
 * forward-declared struct is all the watch API needs.
 */
extern struct tiku_process tiku_shell_process;

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Strict signed-decimal parse with overflow guard.
 *
 * Parses @p s as a base-10 long: an optional leading '+' or '-' sign
 * followed by one or more ASCII digits, and nothing else.  Unlike the
 * libc strtol() family this is intentionally unforgiving — there is no
 * leading-whitespace skip, no trailing-garbage tolerance, and no
 * partial parse.  Any non-digit byte after the sign, or an empty
 * string after the sign, fails outright.  This strictness is what lets
 * rules_evaluate() decide cleanly whether a VFS reading is "numeric"
 * (so the ordering operators apply) or "textual" (so == / != fall back
 * to strcmp): a value like "battery" or "8000000\n" with stray bytes
 * simply reports not-a-number.
 *
 * Overflow is detected before it happens via the standard
 * (val > (LONG_MAX - digit) / 10) test, so a value too large for a
 * long is rejected rather than wrapping.  @p out is written only on
 * success; on any failure it is left untouched.
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
 * @brief Copy a NUL-terminated string into a fixed field with a cap.
 *
 * Copies @p src into @p dst, writing at most @p cap-1 characters plus
 * the terminating NUL, so @p dst is always NUL-terminated on success.
 * Used to load the path, value, and action fields of a rule slot from
 * caller-supplied strings while enforcing the per-field maxima
 * (TIKU_SHELL_RULES_PATH_MAX, _VALUE_MAX, _ACTION_MAX).
 *
 * The copy fails (returning 0 without guaranteeing @p dst is
 * terminated) when @p src is longer than the field can hold: after
 * copying cap-1 bytes the function checks src[i] and reports overflow
 * if that byte is not the NUL.  This is the failure that surfaces to
 * the user as "path/value too long" via tiku_shell_rules_add_argv().
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
 * Recognises the six textual comparison operators a user types after
 * the path in an "on <path> <op> <value> ..." rule and maps them to
 * the corresponding tiku_shell_rule_op_t enumerator: ">" -> OP_GT,
 * "<" -> OP_LT, ">=" -> OP_GE, "<=" -> OP_LE, "==" -> OP_EQ,
 * "!=" -> OP_NE.  @p out is written only when a token matches.
 *
 * The seventh operator, OP_CHANGED, is intentionally NOT parsed here:
 * "changed" uses a different grammar ("on changed <path> ...") and is
 * detected separately in tiku_shell_rules_add_argv() before this
 * helper is reached.
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
 * Computes the boolean truth of the comparison between a freshly read
 * VFS value (@p lhs) and the rule's stored right-hand side (@p rhs).
 * Both sides are first run through rules_parse_long() to learn whether
 * each is a clean signed decimal.
 *
 * Ordering operators (>, <, >=, <=) are strictly numeric: the relation
 * is true only when BOTH sides parse as numbers and the arithmetic
 * comparison holds.  If either side is non-numeric the result is false
 * — there is no sensible lexical ordering for sensor readings, so a
 * malformed or textual node simply never satisfies an ordering rule.
 *
 * Equality and inequality (==, !=) compare numerically when both sides
 * parse, so "40" == "40\0" after whitespace stripping behaves as
 * expected; otherwise they fall back to a lexical strcmp on the raw
 * strings.  This dual mode is what makes
 * "/sys/power/source == battery" compare text while "/dev/temp0 > 40"
 * compares magnitude.
 *
 * OP_CHANGED never reaches this function in normal operation — the
 * tick handles it before calling here — but the switch returns 0
 * defensively if it ever does, and any unknown enum value also yields
 * 0 (no match).
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
 * Called once at shell startup.  Because rule_table lives in BSS and a
 * free slot is encoded as TIKU_SHELL_RULE_FREE (== 0), the table is
 * already in its empty state by the time C runtime startup has zeroed
 * BSS; there is nothing to clear here.  The function is retained for
 * symmetry with the other shell subsystems' init hooks and as the
 * natural place to wire in FRAM-backed rule recovery should storage
 * move off SRAM later.
 */
/**
 * @brief Re-derive every rule's trigger path from current state.
 *
 * The watch-subscription strategy is wholesale: drop every watch the
 * shell process holds, then walk the table and re-subscribe each
 * ACTIVE rule whose path resolves to a writable node.  Because
 * tiku_vfs_watch() is idempotent and unwatch_all() is one call, this
 * is simpler and safer than tracking which subscription belonged to
 * which (possibly just-deleted) rule — and at TIKU_SHELL_RULES_MAX
 * of 4 the wholesale walk is a handful of path resolutions.
 *
 * Side effect on every ACTIVE rule: r->node is (re)cached.
 *   - node with a write handler  -> event-armed: watched, and the
 *     poll tick skips it (tiku_shell_rules_on_vfs() evaluates it)
 *   - node without write handler -> sensor-side: stays on the poll
 *     tick (its value changes without writes, so no event fires)
 *   - unresolvable path          -> node = NULL: stays on the poll
 *     tick, which retries the read each pass (pre-watch behaviour
 *     for paths that appear later)
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
 * Scans rule_table in index order for a slot whose state is
 * TIKU_SHELL_RULE_FREE and populates it.  The path, value, and action
 * strings are copied into the slot's fixed fields via
 * rules_copy_field(), enforcing the per-field length caps; if any one
 * overflows its field the slot is left effectively unpublished and the
 * call fails.  NULL pointers for path, value, or action are rejected
 * up front.
 *
 * Publication order matters: every other field of the slot is written
 * before its state is set to TIKU_SHELL_RULE_ACTIVE last.  The tick
 * only ever looks at ACTIVE slots, so this ordering ensures the tick
 * never observes a half-initialised rule even though both run in the
 * same single-threaded shell context.
 *
 * last_match is zeroed so the new rule starts un-edged: a comparison
 * rule whose condition is already true on the first tick after add
 * still fires once (false -> true), and a CHANGED rule baselines on
 * its first tick without firing.
 *
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
 * Marks the slot at @p id free by setting its state to
 * TIKU_SHELL_RULE_FREE; the slot becomes reusable by the next
 * tiku_shell_rules_add().  The string fields are not wiped — they are
 * dead once the slot is free and are overwritten on the next add — so
 * this is an O(1) state flip.
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
 * Walks the whole table and flips each non-free slot back to
 * TIKU_SHELL_RULE_FREE, returning how many were actually active.  This
 * backs the "rules clear" command; like the single-slot delete it only
 * touches the state field, so it is cheap regardless of how many rules
 * were registered.
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
 * Returns a const pointer into rule_table for the slot at @p id so the
 * "rules" command can render the path, operator, value, and action of
 * each live rule (operator names come from tiku_shell_rules_op_name()).
 * The pointer aliases live engine storage; callers must treat it as
 * read-only and must not retain it across a tiku_shell_rules_del() /
 * _clear() that could free the slot.
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
 * Inverse of rules_parse_op() extended to cover OP_CHANGED: returns
 * the canonical text for each operator (">", "<", ">=", "<=", "==",
 * "!=", or "changed") for display by the "rules" command.  Any value
 * outside the known enumerators renders as "?".  The returned pointer
 * is a string literal with static lifetime; the caller must not modify
 * or free it.
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
 * Reassembles the action portion of an "on" command line, which the
 * parser has already split into separate argv tokens, back into one
 * space-separated string suitable for storage in a rule's action field
 * and for later re-tokenising by tiku_shell_parser_execute().  A single
 * space is inserted between tokens (never before the first, never a
 * trailing one), so "led on 0" round-trips intact.  Note this collapses
 * any run of whitespace the user typed between tokens to a single
 * space, which is harmless for command dispatch.
 *
 * The bound check (pos >= outsz-1) is applied before writing each byte
 * — both the separators and the token characters — so @p out is never
 * overrun and is always NUL-terminated on success.
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
 * This is the full front end for the "on" command: it accepts the two
 * rule grammars, validates them, joins the trailing tokens into an
 * action string, and hands the result to tiku_shell_rules_add().  All
 * user-facing diagnostics for the command are emitted here via
 * SHELL_PRINTF; on success the function is silent and the new rule
 * shows up under the "rules" command.
 *
 * Two grammars are accepted:
 *   - Comparison: "on <path> <op> <value> <command...>" — argv[1] is
 *     the path, argv[2] the operator (parsed by rules_parse_op()),
 *     argv[3] the right-hand value, and argv[4..] the action.  Needs
 *     at least 5 arguments.
 *   - Change:     "on changed <path> <command...>" — selected when
 *     argv[1] is the literal "changed".  op becomes OP_CHANGED, the
 *     path is argv[2], the stored value starts empty ("") because the
 *     tick repurposes the value field to hold the last-seen reading,
 *     and the action is argv[3...].
 *
 * Failure modes each print a targeted usage or error line and return
 * -1: too few arguments, an unrecognised operator, an action that
 * overflows TIKU_SHELL_RULES_ACTION_MAX once joined, or a rejection
 * from tiku_shell_rules_add() (path/value too long, or no free slot).
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
 * tiku_shell_parser_execute() tokenises its argument destructively
 * (writing NUL bytes at space boundaries), so a rule's action must
 * never be passed to it directly — that would corrupt the stored rule.
 * This helper copies the action field into a caller-owned scratch
 * buffer (actionbuf, sized TIKU_SHELL_RULES_ACTION_MAX in the tick)
 * which the parser may then chew up freely.  The copy stops at the
 * source NUL and the destination's last byte is force-terminated, so
 * actionbuf is always a valid string even if the action somehow filled
 * its field.
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

/**
 * @brief Trigger paths: poll tick vs. watch event.
 *
 * Since the watch conversion there are two ways a rule gets
 * evaluated, sharing one evaluator (rules_eval_one) so semantics
 * are identical:
 *
 *   - POLL (tiku_shell_rules_tick, once per shell tick): carries
 *     sensor-side rules — nodes without a write handler, whose
 *     values change in the world rather than through
 *     tiku_vfs_write() — plus rules whose path did not resolve at
 *     arm time (retried each pass, matching pre-watch behaviour).
 *     Event-armed rules are skipped here entirely: their per-tick
 *     cost, including side-effectful reads like ADC conversions,
 *     is gone.
 *
 *   - EVENT (tiku_shell_rules_on_vfs, on TIKU_EVENT_VFS): carries
 *     rules whose node is writable.  rules_rearm() subscribed them
 *     via tiku_vfs_watch(); every successful write to the node
 *     posts the event and the matching rules evaluate immediately
 *     — write-to-reaction latency is one event dispatch instead of
 *     up to a full poll period, and a value that pulses between
 *     ticks can no longer be missed.
 *
 * Evaluation semantics (both paths, in rules_eval_one): read the
 * path into a stack buffer; strip the trailing '\n'/'\r'/' ' run
 * (VFS handlers append a newline by convention); read failure
 * clears last_match so the rule re-baselines/re-edges when the
 * path returns.  OP_CHANGED baselines on first evaluation and then
 * fires on any difference, updating the baseline; comparison ops
 * fire only on a false->true edge.  A firing rule dispatches its
 * action synchronously through tiku_shell_parser_execute() via a
 * scratch copy (the parser tokenises in place), exactly as if
 * typed at the prompt.
 *
 * Loop note: an action that writes its own watched node re-enters
 * evaluation via a fresh event rather than waiting a tick.  The
 * edge semantics still bound it — a sustained-true comparison does
 * not re-fire, and a CHANGED rule whose action rewrites the same
 * value reads back equal to its baseline — but an action that
 * alternates its own trigger value now oscillates at event speed
 * instead of tick speed.  Same user error as before, faster.
 */
/**
 * @brief Evaluate one rule, exactly as the original tick did.
 *
 * Shared by the poll tick and the event path so both trigger paths
 * have byte-identical semantics: read the path, strip the trailing
 * newline run, baseline-or-compare for CHANGED, edge-detect for
 * comparison ops, dispatch the action through a scratch copy.  The
 * ~96 bytes of read/action buffers live on the caller's stack only
 * for the duration of the call.
 *
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

    n = tiku_vfs_read(r->path, readbuf, sizeof(readbuf) - 1);
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
