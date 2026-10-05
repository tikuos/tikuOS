/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_rules.h - reactive rule engine for the shell.
 *
 * A rule is "if VFS_path OP value then run COMMAND", evaluated on each write
 * to a watched writable node and on the shell tick otherwise.  A comparison
 * fires on a false-to-true transition, `changed` on any change.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_RULES_H_
#define TIKU_SHELL_RULES_H_

#include <stdint.h>
#include <kernel/vfs/tiku_vfs.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Maximum number of concurrent rules. */
#ifndef TIKU_SHELL_RULES_MAX
#define TIKU_SHELL_RULES_MAX           4
#endif

/** Size of a rule's path buffer, NUL included (fits /sys/watchdog/mode). */
#ifndef TIKU_SHELL_RULES_PATH_MAX
#define TIKU_SHELL_RULES_PATH_MAX      24
#endif

/** Size of a rule's value buffer, NUL included; a reading is cut to fit. */
#ifndef TIKU_SHELL_RULES_VALUE_MAX
#define TIKU_SHELL_RULES_VALUE_MAX     12
#endif

/** Size of a rule's action buffer, NUL included. */
#ifndef TIKU_SHELL_RULES_ACTION_MAX
#define TIKU_SHELL_RULES_ACTION_MAX    40
#endif

/*---------------------------------------------------------------------------*/
/* TYPES                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Slot state; TIKU_SHELL_RULE_FREE is the zeroed "empty" marker. */
typedef enum {
    TIKU_SHELL_RULE_FREE = 0,
    TIKU_SHELL_RULE_ACTIVE
} tiku_shell_rule_state_t;

/** @brief A rule's operator: a comparison, or a change of value. */
typedef enum {
    TIKU_SHELL_RULE_OP_GT,
    TIKU_SHELL_RULE_OP_LT,
    TIKU_SHELL_RULE_OP_GE,
    TIKU_SHELL_RULE_OP_LE,
    TIKU_SHELL_RULE_OP_EQ,
    TIKU_SHELL_RULE_OP_NE,
    TIKU_SHELL_RULE_OP_CHANGED   /**< value[] holds the last seen reading */
} tiku_shell_rule_op_t;

/** @brief A single reactive rule.
 *
 * For a comparison, @c value is the fixed right-hand side and @c last_match
 * the previous match state, for edge detection.  For OP_CHANGED, @c value
 * holds the last seen reading and @c last_match says a baseline is set.
 */
typedef struct {
    tiku_shell_rule_state_t state;
    tiku_shell_rule_op_t    op;
    uint8_t                 last_match;
    uint8_t                 armed;      /**< 1 while the node's watch is
                                             held: events evaluate the
                                             rule, the poll tick skips it */
    /** @brief Resolved node, cached at (re-)arm time.
      *  A writable node whose watch is held is event-armed; any other
      *  node is on the poll path; NULL = the path did not resolve,
      *  and the poll path retries. */
    const tiku_vfs_node_t  *node;
    char                    path[TIKU_SHELL_RULES_PATH_MAX];
    char                    value[TIKU_SHELL_RULES_VALUE_MAX];
    char                    action[TIKU_SHELL_RULES_ACTION_MAX];
} tiku_shell_rule_t;

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the rule engine; drops every VFS watch the shell process
 *        holds.
 * @note The shell calls it once at startup.
 */
void tiku_shell_rules_init(void);

/**
 * @brief Register a rule in the first free slot.
 *
 * @note Re-arms every rule: drops each VFS watch the shell process holds,
 *       the `watch` command's and BASIC's included, and watches the rules'
 *       writable nodes again.
 * @param path    VFS path the rule reads (must fit PATH_MAX-1).
 * @param op      Comparison operator.
 * @param value   Right-hand side (must fit VALUE_MAX-1).
 * @param action  Command line dispatched each time the rule fires
 *                (must fit ACTION_MAX-1).
 * @return Slot id (>= 0) on success, -1 if a pointer is NULL, the table
 *         is full or a field overflows its buffer.
 */
int8_t tiku_shell_rules_add(const char *path,
                             tiku_shell_rule_op_t op,
                             const char *value,
                             const char *action);

/**
 * @brief Free a rule slot by id, then re-arm as tiku_shell_rules_add() does.
 * @return 0 on success, -1 if id is out of range or already free.
 */
int8_t tiku_shell_rules_del(uint8_t id);

/**
 * @brief Free every active rule slot and drop every VFS watch the shell
 *        process holds.
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
 * @brief Register a rule from the argv of the `on` command.
 *
 * Comparison grammar: argv[1] path, argv[2] op, argv[3] value, argv[4..]
 * action.  Change grammar: argv[1] "changed", argv[2] path, argv[3..] action.
 * Action tokens join with single spaces; errors print via SHELL_PRINTF.
 *
 * @return Slot id (>= 0) on success, -1 on error (message printed).
 */
int8_t tiku_shell_rules_add_argv(uint8_t argc, const char *argv[]);

/**
 * @brief Evaluate every active rule that is not event-armed.
 *
 * These are rules on nodes without a write handler, rules whose path did not
 * resolve and rules whose watch was refused.  Evaluation is the event path's:
 * a comparison fires on a false-to-true edge, and a failed read is no match.
 *
 * @note The shell main loop calls it on each poll.
 */
void tiku_shell_rules_tick(void);

/**
 * @brief Evaluate every active rule whose cached node is @p node_ptr, as the
 *        poll tick would.
 *
 * @note The shell process calls it on each TIKU_EVENT_VFS.
 * @param node_ptr  The changed node, as delivered in the event data; NULL
 *                  does nothing
 */
void tiku_shell_rules_on_vfs(const void *node_ptr);

#endif /* TIKU_SHELL_RULES_H_ */
