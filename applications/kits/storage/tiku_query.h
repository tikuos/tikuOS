/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_query.h - searching the namespace.
 *
 * A query is a predicate plus a scope, exactly as it is in Tracker: terms are
 * (attribute, operator, value), terms join with and/or, and the scope is
 * enforced OUTSIDE the predicate as a path-prefix test on each result -- the
 * same split the original uses, and the reason a folder filter can be added
 * to a running search without rewriting it.
 *
 * The matching rules are ported rather than invented, including the two that
 * look odd until you see the source:
 *
 *   - a name search is a SUBSTRING match, unless the typed text contains a
 *     '*' anywhere, in which case the whole text becomes a pattern and must
 *     match the name outright;
 *   - case-insensitivity is not a comparison flag but a rewrite of the value
 *     into per-character classes, so "txt" is matched as "[tT][xX][tT]" and
 *     one matcher serves every operator.
 *
 * On TikuOS a volume is a namespace subtree and the type registry is the
 * node's own typed descriptor, so a query can ask for "every writable node
 * under /dev whose value is over 1000" without an index existing anywhere.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_QUERY_H_
#define TIKU_QUERY_H_

#include "tiku_attr.h"
#include "tiku_model.h"

#include <stdint.h>

#define TIKU_QUERY_TERMS_MAX   6
#define TIKU_QUERY_SCOPES_MAX  4
#define TIKU_QUERY_VALUE_MAX  64
#define TIKU_QUERY_PRED_MAX  512

/** @brief Which field a term tests. */
typedef enum {
    TIKU_QF_NAME = 0,
    TIKU_QF_PATH,
    TIKU_QF_KIND,
    TIKU_QF_VALUE,
    TIKU_QF_SIZE,
    TIKU_QF_CAP,
    TIKU_QF_UNIT,        /* the descriptor's unit: bytes, Hz, s ...    */
    TIKU_QF_WRITABLE,
    /* The entry's TYPE, matched as a type rather than as text: picking a
     * supertype admits everything under it (Q-004). */
    TIKU_QF_TYPE,
    TIKU_QF_MODIFIED     /* seconds since epoch, with date syntax     */
} tiku_qfield_t;

/** @brief The operator vocabulary, in the original's order. */
typedef enum {
    TIKU_OP_CONTAINS = 0,
    TIKU_OP_IS,
    TIKU_OP_IS_NOT,
    TIKU_OP_BEGINS_WITH,
    TIKU_OP_ENDS_WITH,
    TIKU_OP_GREATER,
    TIKU_OP_LESS
} tiku_qop_t;

/** @brief How a term joins the one before it. */
typedef enum {
    TIKU_JOIN_AND = 0,
    TIKU_JOIN_OR
} tiku_qjoin_t;

typedef struct {
    tiku_qfield_t field;
    tiku_qop_t    op;
    char              value[TIKU_QUERY_VALUE_MAX];
    tiku_qjoin_t  join;      /* how it joins the PREVIOUS term         */
    int               pattern_ready; /* formula already supplied a glob   */
} tiku_qterm_t;

/** @brief What the panel is asking. */
typedef enum {
    TIKU_QMODE_NAME = 0,
    TIKU_QMODE_ATTRIBUTE,
    TIKU_QMODE_FORMULA
} tiku_qmode_t;

/** @brief What a saved record IS, which decides what opening it does. */
typedef enum {
    TIKU_QREC_QUERY = 0,     /* opening it runs it                    */
    TIKU_QREC_TEMPLATE       /* opening it opens the panel, filled in */
} tiku_qrec_t;

/**
 * @brief What a scope was when it was written.
 *
 * A path alone cannot say whether the volume it names is the one that was
 * searched: mount points are reused.  The identity is carried beside it and
 * checked on the way back, so a query saved against a disk that is no longer
 * there says so rather than quietly searching whatever took its place
 * (Q-027).
 */
typedef struct {
    char     path[TIKU_PATH_MAX];
    uint64_t dev;                /* 0 when it was not recorded            */
    char     volume[TIKU_NAME_MAX];
} tiku_qscope_t;

typedef struct {
    tiku_qmode_t mode;
    tiku_qterm_t term[TIKU_QUERY_TERMS_MAX];
    int              term_count;
    /* Scope: the subtrees a result may come from.  None means the whole
     * namespace, which is the "All disks" case. */
    char             scope[TIKU_QUERY_SCOPES_MAX][TIKU_PATH_MAX];
    int              scope_count;
    /* What each scope was, in the same order.  Kept beside the paths rather
     * than replacing them so a v1 record still loads. */
    tiku_qscope_t scope_was[TIKU_QUERY_SCOPES_MAX];
    int              max_results;
    /* Trash is excluded unless the panel explicitly includes it.  The path
     * supplements the model kind so a query scoped directly inside Trash
     * still knows that its children belong to the excluded subtree. */
    int              include_trash;
    char             trash_path[TIKU_PATH_MAX];
    /* A formula that did not parse must not degenerate into an empty
     * predicate, since an empty predicate matches the whole namespace. */
    int              invalid;
    /* Relative Modified terms move with the wall clock and must be
     * re-evaluated at their next minute/hour/day boundary (Q-009). */
    int              dynamic_date;
    /* What the record is and how it should be treated.  A query the user
     * never named is TEMPORARY: it is a by-product of a search, and it is
     * swept up later rather than accumulating for ever (MA-053, AW-090). */
    tiku_qrec_t  kind;
    int              temporary;
    int64_t          saved_at;   /* seconds; 0 when never written         */
    /* The type the panel had selected, ANDed onto everything (Q-004). */
    char             type[TIKU_TYPE_MAX];
    /* Lines a record carried that could not be placed.  Kept because a
     * record that half-loaded is worth knowing about: silently dropping
     * part of a saved search is how a query comes back meaning something
     * else. */
    int              dropped;
} tiku_query_t;

/** @brief Start an empty query over the whole namespace. */
void tiku_query_init(tiku_query_t *q);

/**
 * @brief Set the query from typed text, as by-name mode does.
 *
 * The '*' rule applies: with a star anywhere the text is a pattern matched
 * outright; without one it is a substring.  Empty text matches everything,
 * because the substring form degenerates to "*" -- which is the original's
 * behaviour and not an accident worth 'fixing'.
 */
void tiku_query_set_name(tiku_query_t *q, const char *text);

/** @brief Parse the predicate grammar emitted by query_predicate(). */
int tiku_query_set_formula(tiku_query_t *q, const char *formula);

/** @brief Append a term.  @return its index, or -1. */
int tiku_query_add_term(tiku_query_t *q, tiku_qfield_t field,
                            tiku_qop_t op, const char *value,
                            tiku_qjoin_t join);

/** @brief Restrict results to a subtree.  @return 1 when it was added. */
int tiku_query_add_scope(tiku_query_t *q, const char *path);

void tiku_query_clear_scope(tiku_query_t *q);

/** @brief Record the current identities of scopes that have none yet. */
void tiku_query_bind_scopes(tiku_query_t *q,
                                tiku_backend_t *backend);

/**
 * @brief Render the predicate the way the formula field shows it.
 *
 * Switching a panel into formula mode pre-fills it with this, so what the
 * controls were asking is visible and editable rather than hidden.
 */
int tiku_query_predicate(const tiku_query_t *q, char *out,
                             size_t max);

/** @brief Whether a scope admits @p path, as a path-prefix test. */
int tiku_query_in_scope(const tiku_query_t *q, const char *path);

/** @brief Whether a node is outside Trash, or Trash was explicitly included. */
int tiku_query_allows_path(const tiku_query_t *q,
                               const tiku_model_t *m);

/** @brief Whether one node satisfies the predicate (scope not considered). */
int tiku_query_matches(const tiku_query_t *q,
                           const tiku_model_t *m,
                           const tiku_desc_t *d, const char *value);

/** @brief Match using an explicit wall-clock time (dynamic-date tests). */
int tiku_query_matches_at(const tiku_query_t *q,
                              const tiku_model_t *m,
                              const tiku_desc_t *d, const char *value,
                              int64_t now);

/** @brief Next wall-clock boundary a dynamic query should refresh at. */
int64_t tiku_query_next_refresh(const tiku_query_t *q, int64_t now);

/**
 * @brief Match @p name against @p pattern, with '*', '?' and [class].
 *
 * Exposed because it is the one matcher every operator goes through, and
 * because its behaviour is worth testing on its own.
 */
int tiku_query_glob(const char *pattern, const char *name);

/**
 * @brief Build the case-insensitive pattern for @p value.
 *
 * Each letter becomes a two-member class and each space becomes '*', which is
 * how the original spells "case-insensitive" -- and why searching "foo bar"
 * also finds "foo-bar".
 */
int tiku_query_pattern(const char *value, tiku_qop_t op, char *out,
                           size_t max);

/*---------------------------------------------------------------------------*/
/* Running one                                                               */
/*---------------------------------------------------------------------------*/

typedef struct tiku_qrun tiku_qrun_t;

/** @brief Begin a search.  Results arrive as the walk proceeds. */
tiku_qrun_t *tiku_query_start(tiku_backend_t *b,
                                      const tiku_query_t *q);

void tiku_query_stop(tiku_qrun_t *r);

/**
 * @brief Advance the walk by at most @p budget directories.
 *
 * Stepped rather than blocking, for the same reason the original's directory
 * read is: a search over a slow device must not stop the window redrawing.
 *
 * @return New results this step, or -1 when finished.
 */
int tiku_query_step(tiku_qrun_t *r, int budget);

int tiku_query_count(const tiku_qrun_t *r);

const tiku_model_t *tiku_query_at(const tiku_qrun_t *r, int i);

/** @brief How many directories have been visited, for a progress line. */
int tiku_query_visited(const tiku_qrun_t *r);

int tiku_query_done(const tiku_qrun_t *r);

/** @brief Whether this live run searches the volume identified by @p dev. */
int tiku_query_targets_device(const tiku_qrun_t *r, uint64_t dev);

/** @brief Type whose results layout this run shares, or empty for any type. */
const char *tiku_query_search_type(const tiku_qrun_t *r);

/*---------------------------------------------------------------------------*/
/* Live queries                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Why a result left the set.
 *
 * The distinction is load-bearing: a node that no longer matches means the
 * ROW goes, while a node that is gone may also mean the thing the window is
 * about is gone.  A single "removed" event cannot tell them apart, and the
 * source that produced it is not recoverable afterwards -- so it is recorded
 * at the moment the difference is still knowable.
 */
typedef enum {
    TIKU_QGONE_UNMATCHED = 0,   /* still there, no longer qualifies   */
    TIKU_QGONE_VANISHED         /* the node itself has gone           */
} tiku_qgone_t;

/** @brief What changed between two runs. */
typedef struct {
    int              added;
    int              removed;
    int              vanished;      /* of the removed, how many were gone */
} tiku_qdiff_t;

/**
 * @brief Re-run the query and report what changed.
 *
 * The result set is NOT cleared first: survivors keep their place, so a
 * window does not flicker or lose its selection every time a value moves.
 *
 * The subscription lives as long as the run does; a caller that stops the run
 * when the first fill completes has silently turned liveness off.
 *
 * @return 0, or -1 on failure.
 */
int tiku_query_refresh(tiku_qrun_t *r, tiku_qdiff_t *out);

/** @brief Whether result @p i arrived in the most recent refresh. */
int tiku_query_is_new(const tiku_qrun_t *r, int i);

/*---------------------------------------------------------------------------*/
/* Saved queries                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Where a saved query is kept, and under what attribute. */
#define TIKU_QUERY_STORE_NODE "\x01queries"
#define TIKU_QUERY_ATTR       "_trk/query"
#define TIKU_QUERY_INDEX_ATTR "_trk/query_index"

struct tiku_store;

/**
 * @brief Write @p q under @p name.
 *
 * Tracker saves a query as a FILE whose attributes carry the predicate, and
 * opening that file runs it.  There is no such file here, so the same fields
 * go to the state store under the query's name -- the indirection is kept
 * (a query is a thing you save and re-open, not just a command you re-type)
 * even though the container is different.
 *
 * @return 0, or -1 when the store refused.
 */
/** @brief The query as its saved text form.  Length, or -1. */
int tiku_query_serialise(const tiku_query_t *q, char *buf,
                             size_t max);

int tiku_query_save(const tiku_query_t *q, const char *name,
                        struct tiku_store *store);

/** @brief Read back a saved query.  @return 0, or -1 when absent. */
int tiku_query_load(tiku_query_t *q, const char *name,
                        struct tiku_store *store);

/** @brief How many saved records the index is read into at once. */
#define TIKU_QUERY_LIST_MAX 32

/**
 * @brief Names of the saved queries, newest first.
 *
 * @return How many were written to @p names.
 */
int tiku_query_list(struct tiku_store *store, char names[][128],
                        int max);

/** @brief Forget a saved query. */
int tiku_query_forget(const char *name, struct tiku_store *store);

/**
 * @brief The saved records of ONE kind, newest first (Q-031, Q-032).
 *
 * The two menus the panel offers are the same index read twice: every
 * template, and the queries that were actually run.  Reading the index
 * once and filtering here keeps the newest-first order both of them
 * depend on.
 *
 * @return how many were written to @p names.
 */
int tiku_query_list_kind(struct tiku_store *store,
                             tiku_qrec_t kind, char names[][128],
                             int max);

/** @brief How old a temporary query may get before the sweep takes it. */
#define TIKU_QUERY_STALE_SEC  (7 * 24 * 60 * 60)

/** @brief How long after a start the sweep holds off (AW-090). */
#define TIKU_QUERY_SWEEP_WAIT (30 * 60)

/**
 * @brief Delete the temporary records nobody is looking at (Q-037, AW-090).
 *
 * A search nobody named is a by-product: it is kept long enough to be
 * re-opened and then reaped.  Three things spare a record -- being a
 * template or a named query, being younger than a week, or being SHOWN in
 * a window right now, because deleting the record behind an open window
 * would leave the window unable to say what it is.
 *
 * @param now      Seconds, the same clock saved_at is stamped from.
 * @param up_for   Seconds since this Tracker started.  The sweep does not
 *                 run in the first half hour: a machine that has just come
 *                 up is busy, and this is the least urgent work there is.
 * @param open     Names currently shown in a window; may be NULL.
 * @return how many were deleted, or -1 when it did not run.
 */
int tiku_query_sweep(struct tiku_store *store, int64_t now,
                         int64_t up_for, const char *const *open, int nopen);

#endif /* TIKU_QUERY_H_ */
