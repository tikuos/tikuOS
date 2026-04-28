/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_rules.h - Reactive rule engine for the shell
 *
 * A reactive rule is a stored "if VFS_path OP value then run COMMAND"
 * triple.  Rules are evaluated on every shell tick: the configured
 * VFS path is read, the relation is checked against the right-hand
 * side, and the action is dispatched through the shell parser
 * exactly when the condition transitions from false to true
 * (edge-triggered).  This makes rules safe to combine with side-
 * effecting actions: a rule that turns on an LED when a temperature
 * threshold is crossed will not fire repeatedly while the temperature
 * stays above threshold.
 *
 * Rule storage today is SRAM-only; the structure leaves room for a
 * later FRAM-backed migration with a magic-number ring like the
 * history subsystem.
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

#ifndef TIKU_SHELL_RULES_H_
#define TIKU_SHELL_RULES_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Maximum number of concurrent rules. */
#ifndef TIKU_SHELL_RULES_MAX
#define TIKU_SHELL_RULES_MAX           4
#endif

/** Maximum VFS path length stored per rule (covers e.g. /sys/watchdog/mode). */
#ifndef TIKU_SHELL_RULES_PATH_MAX
#define TIKU_SHELL_RULES_PATH_MAX      24
#endif

/** Maximum right-hand-side comparison value length. */
#ifndef TIKU_SHELL_RULES_VALUE_MAX
#define TIKU_SHELL_RULES_VALUE_MAX     12
#endif

/** Maximum action command length. */
#ifndef TIKU_SHELL_RULES_ACTION_MAX
#define TIKU_SHELL_RULES_ACTION_MAX    40
#endif

/*---------------------------------------------------------------------------*/
/* TYPES                                                                     */
/*---------------------------------------------------------------------------*/

typedef enum {
    TIKU_SHELL_RULE_FREE = 0,
    TIKU_SHELL_RULE_ACTIVE
} tiku_shell_rule_state_t;

typedef enum {
    TIKU_SHELL_RULE_OP_GT,
    TIKU_SHELL_RULE_OP_LT,
    TIKU_SHELL_RULE_OP_GE,
    TIKU_SHELL_RULE_OP_LE,
    TIKU_SHELL_RULE_OP_EQ,
    TIKU_SHELL_RULE_OP_NE,
    TIKU_SHELL_RULE_OP_CHANGED   /**< value[] holds the last seen reading */
} tiku_shell_rule_op_t;

/** A single reactive rule.
 *
 * Field semantics depend on @c op:
 *   - Comparison ops: @c value is the user-supplied right-hand side
 *     (immutable); @c last_match tracks the previous tick's match
 *     state for false->true edge detection.
 *   - OP_CHANGED:     @c value is the last seen VFS reading
 *     (overwritten on every change); @c last_match is the "baseline
 *     established" flag (0 before the first read, 1 thereafter).
 */
typedef struct {
    tiku_shell_rule_state_t state;
    tiku_shell_rule_op_t    op;
    uint8_t                 last_match;
    uint8_t                 _reserved;
    char                    path[TIKU_SHELL_RULES_PATH_MAX];
    char                    value[TIKU_SHELL_RULES_VALUE_MAX];
    char                    action[TIKU_SHELL_RULES_ACTION_MAX];
} tiku_shell_rule_t;

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the rule engine.  Called once at shell startup.
 */
void tiku_shell_rules_init(void);

/**
 * @brief Register a rule in the first free slot.
 *
 * @param path    VFS path to read each tick (must fit PATH_MAX-1).
 * @param op      Comparison operator.
 * @param value   Right-hand side (must fit VALUE_MAX-1).
 * @param action  Command line dispatched on a false->true transition
 *                (must fit ACTION_MAX-1).
 * @return Slot id (>= 0) on success, -1 if the table is full or a
 *         field overflows its buffer.
 */
int8_t tiku_shell_rules_add(const char *path,
                             tiku_shell_rule_op_t op,
                             const char *value,
                             const char *action);

/**
 * @brief Free a rule slot by id.
 * @return 0 on success, -1 if id is out of range or already free.
 */
int8_t tiku_shell_rules_del(uint8_t id);

/**
 * @brief Free every active rule slot.
 *
 * @return Number of slots that were active and have been freed.
 */
uint8_t tiku_shell_rules_clear(void);

/**
 * @brief Read-only inspection of a rule slot.
 * @return Pointer to the rule, or NULL if the slot is free or invalid.
 */
const tiku_shell_rule_t *tiku_shell_rules_get(uint8_t id);

/**
 * @brief Convert an operator enum to its printable token (">", "==", etc.).
 */
const char *tiku_shell_rules_op_name(tiku_shell_rule_op_t op);

/**
 * @brief Convenience for the `on` command: parse argv, validate, register.
 *
 * Two grammars are accepted:
 *   - Comparison: argv[1] = path, argv[2] = op, argv[3] = value,
 *                 argv[4..] = action.
 *   - Change:     argv[1] = "changed", argv[2] = path,
 *                 argv[3..] = action.
 *
 * Action tokens are joined with single spaces.  Diagnostics printed
 * via SHELL_PRINTF on error.
 *
 * @return Slot id (>= 0) on success, -1 on error (message printed).
 */
int8_t tiku_shell_rules_add_argv(uint8_t argc, const char *argv[]);

/**
 * @brief Periodic dispatcher; called from the shell main loop.
 *
 * Walks every active rule, reads its VFS path, evaluates the relation,
 * and dispatches the action when the result transitions from false to
 * true (edge-triggered).  A rule whose VFS read fails is treated as
 * non-matching; transitions back to non-match are silent.
 */
void tiku_shell_rules_tick(void);

#endif /* TIKU_SHELL_RULES_H_ */
