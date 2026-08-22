/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_notify.h - change notices, and where they come from.
 *
 * Tracker feeds node-monitor, path-monitor and live-query notices through ONE
 * handler, which is why a query window updates by the same code as a folder
 * window.  Everything below depends on that single entry point existing, so
 * it is built first and the source is made to fit it -- not the other way
 * round.
 *
 * The device cannot push these yet.  Its watch primitive is per-node, has
 * eight slots, occupies the shell while it runs, and reports a value with no
 * opcode -- it cannot say "created" or "renamed", and it cannot cover a
 * folder.  So the notices are DERIVED here, by diffing a re-listing against
 * what the view already holds.  The vocabulary is the real one, and when the
 * device grows a channel that can carry it, only the source changes; the
 * handler and every rule it enforces stay as they are.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_NOTIFY_H_
#define TIKU_NOTIFY_H_

#include "tiku_model.h"

/** @brief What happened to a node. */
typedef enum {
    TIKU_EV_CREATED = 0,
    TIKU_EV_REMOVED,
    TIKU_EV_MOVED,        /* renamed, or moved between directories    */
    TIKU_EV_CHANGED,      /* stat changed: size, mtime, permissions   */
    TIKU_EV_ATTR          /* a value or descriptor changed            */
} tiku_evop_t;

/**
 * @brief One notice.
 *
 * It carries the model rather than a bare reference, because a notice that
 * cannot describe what it is about cannot be parked and replayed -- and
 * parking is what stops a change arriving a moment before its row from being
 * lost.
 */
/* Which FACTS a CHANGED/ATTR notice concerns (MA-072).  Zero means the
 * notice cannot say, and everything the row shows must be re-read --
 * which is also what every notice meant before the mask existed. */
#define TIKU_EVF_SIZE  0x01u
#define TIKU_EVF_DATE  0x02u
#define TIKU_EVF_KIND  0x04u   /* kind, type, icon-bearing facts   */
#define TIKU_EVF_VALUE 0x08u   /* value/meta: Value and attr columns */
#define TIKU_EVF_CAP   0x10u
#define TIKU_EVF_PERM  0x20u

typedef struct {
    tiku_evop_t  op;
    char             path[TIKU_PATH_MAX];
    char             from[TIKU_PATH_MAX];  /* MOVED: the old path    */
    char             dir[TIKU_PATH_MAX];   /* the parent it concerns */
    tiku_model_t model;                    /* valid unless REMOVED   */
    int64_t          at;                       /* arrival, microseconds  */
    unsigned         fields;                   /* TIKU_EVF_*, or 0   */
} tiku_node_event_t;

#define TIKU_EVENTS_MAX 128

/** @brief A batch of notices, in arrival order. */
typedef struct {
    tiku_node_event_t ev[TIKU_EVENTS_MAX];
    int              count;
    int              overflowed;   /* more happened than fits: re-list   */
} tiku_evlist_t;

void tiku_evlist_clear(tiku_evlist_t *l);

/** @brief Append a notice.  @return 1 when it fit. */
int tiku_evlist_add(tiku_evlist_t *l, tiku_evop_t op,
                        const tiku_model_t *m, const char *from,
                        int64_t at);

/** @brief Append a notice that can NAME its fields (MA-072). */
int tiku_evlist_add_fields(tiku_evlist_t *l, tiku_evop_t op,
                               const tiku_model_t *m, const char *from,
                               int64_t at, unsigned fields);

/**
 * @brief Derive the notices between two listings of one directory.
 *
 * An entry in @p now and not in @p was is a creation; the reverse is a
 * removal; one whose facts moved is a change.  A removal and a creation that
 * share a node id in the same pass are ONE move, not two events -- reporting
 * them separately would drop the row's selection and its place.
 *
 * @return Notices produced.
 */
int tiku_notify_diff(const tiku_model_t *was, int nwas,
                         const tiku_model_t *now, int nnow,
                         int64_t at, tiku_evlist_t *out);

/**
 * @brief Collapse a burst of notices to one final notice per node.
 *
 * The newest model wins; a CREATED followed by CHANGED remains CREATED and
 * a MOVED keeps the first old path.  This is the in-process equivalent of
 * Tracker's MIME-notice accumulator and is also safe for VFS attribute
 * storms (PVL-094, PVN-054).
 */
int tiku_evlist_coalesce(const tiku_evlist_t *in,
                             tiku_evlist_t *out);

/*---------------------------------------------------------------------------*/
/* Parking                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief How long a notice waits for its row to appear.
 *
 * A change can arrive before the creation it belongs to; dropping it would
 * mean a file altered the instant after it was made shows its old state.
 */
#define TIKU_PARK_LIFETIME_US 10000000

typedef struct {
    tiku_node_event_t ev[32];
    int              count;
} tiku_park_t;

void tiku_park_clear(tiku_park_t *p);

/** @brief Hold a notice for a node the view does not know yet. */
int tiku_park_add(tiku_park_t *p, const tiku_node_event_t *e);

/**
 * @brief Take back every parked notice for @p path, oldest first.
 *
 * Arrival order is preserved: two changes to one node replayed backwards
 * would leave the earlier value showing.
 *
 * @return How many were written to @p out.
 */
int tiku_park_take(tiku_park_t *p, const char *path,
                       tiku_node_event_t *out, int max);

/**
 * @brief Drop notices older than the lifetime.
 *
 * @return How many were discarded.
 */
int tiku_park_expire(tiku_park_t *p, int64_t now);

int tiku_park_count(const tiku_park_t *p);

#endif /* TIKU_NOTIFY_H_ */
