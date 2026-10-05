/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_reclaim.c - bounded, cooperative reconstruction of owned backing.
 *
 * The owner registry, the tickets and the one running job.  Each poll does
 * one bounded step: plan a layout, stop the owners, publish the layout, then
 * let the owners rebuild; an owner that refuses abandons the job.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_reclaim_internal.h"
#include "tiku_mem_internal.h"

#if TIKU_MEM_RECLAIM_ENABLE
#include "kernel/process/tiku_process.h"
#include "hal/tiku_cpu.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if TIKU_MEM_MAX_OWNERS < 1 || TIKU_MEM_MAX_OWNERS > 16
#error Owner masks support 1..16 owners
#endif
#if TIKU_MEM_MAX_TICKETS < 1 || TIKU_MEM_MAX_TICKETS > 255
#error Invalid ticket table size
#endif
/** @brief Highest generation a slot reaches; a slot there is retired. */
#ifndef TIKU_MEM_GENERATION_MAX
#define TIKU_MEM_GENERATION_MAX UINT32_MAX
#endif
/** @brief Reservation records: the size of the job's record arrays. */
#define N TIKU_MEM_MAX_RESERVATIONS
#if N > 64
#error Reconstruction supports at most 64 reservation records
#endif
/** @brief Claim window of a READY ticket: 10 s, within the clock's range. */
#define CLAIM_TICKS ((tiku_clock_time_t)(TIKU_CLOCK_SECOND <= TIKU_CLOCK_MAX_INTERVAL / 10u ? \
                                        10u * TIKU_CLOCK_SECOND : TIKU_CLOCK_MAX_INTERVAL))
/**
 * @brief How long a ticket may wait for its job, and a final one is kept:
 *        30 s, within the clock's range.
 */
#define RETAIN_TICKS ((tiku_clock_time_t)(TIKU_CLOCK_SECOND <= TIKU_CLOCK_MAX_INTERVAL / 30u ? \
                                         30u * TIKU_CLOCK_SECOND : TIKU_CLOCK_MAX_INTERVAL))

/** @brief One registered owner and its progress in the running job. */
typedef struct {
    tiku_mem_owner_registration_t registration;
    char name[24];                  /**< copy of registration.name */
    uint32_t generation;            /**< slot generation in the handle */
    uint8_t live;
    /** Bound process instance; consented to PREPARE; exited during the job;
     *  exited without consent, cleanup due; being started by the job. */
    uint8_t process_generation, consent, stopped, init_failed, starting;
    uint32_t process_restarts;      /**< restarts the coordinator made */
} owner_t;
/** @brief One submitted request and its progress. */
typedef struct {
    tiku_mem_reclaim_request_t request;
    tiku_reclaim_record_t result;   /**< normalized, then the held record */
    tiku_mem_reclaim_status_t status;
    struct tiku_process *process;   /**< submitter, or NULL for the kernel */
    uint32_t generation, sequence;  /**< handle generation; submit order */
    /** Slot in use; the submitter's instance; cancellation requested. */
    uint8_t live, process_generation, cancelling;
} ticket_t;
/** @brief The running reconstruction job; there is at most one. */
typedef struct {
    /** Reservation table the plan is made against; the selected owners'
     *  records; their new places, with the request's at index count. */
    tiku_reclaim_record_t snapshot[N], old[N], layout[N + 1];
    tiku_clock_time_t deadline[TIKU_MEM_MAX_OWNERS];  /**< phase deadlines */
    /** Search order of the layout entries; next candidate at each depth. */
    uint16_t order[N + 1], choice[N + 1];
    uint8_t placed[N + 1];          /**< layout entries placed so far */
    uint32_t generation, steps;     /**< job generation; search steps */
    /** Owner masks: selected; asked during this job; done with the phase;
     *  held again at their original places in ABORT. */
    uint16_t owners, touched, done, old_rebound;
    /** Selected records; search depth; next suffix boundary; owner turn. */
    unsigned count, depth, boundary, turn;
    /** Running; its ticket; tier and span cursors of the planner; search
     *  under way; layout published. */
    uint8_t active, ticket, tier_cursor, span_cursor, searching, committed;
    /** Faulted; credits held; fast layout used (1 low, 2 high); replans. */
    uint8_t fault, credits, fast_path, replans;
} job_t;
static owner_t owners[TIKU_MEM_MAX_OWNERS];
static ticket_t tickets[TIKU_MEM_MAX_TICKETS];
static job_t job;
/** @brief The last job that ended, for the "last" report. */
static struct {
    uint32_t generation, steps;
    tiku_mem_ticket_state_t state;
    tiku_mem_reclaim_cause_t cause;
} last_job;
static tiku_mem_reclaim_stats_t counters;
static uint32_t sequence;          /**< last ticket sequence number */
/** Coordinator on; a poll in progress. */
static uint8_t enabled = 1, polling;
static int callback_owner = -1;    /**< owner whose callback runs, or -1 */
/* Defined in tiku_reclaim_process.inl. */
static int process_owner_current(unsigned oi);
static tiku_mem_owner_result_t process_step(unsigned oi);

/** @brief Non-zero when two owner handles are equal. */
static int owner_equal(tiku_mem_owner_t a, tiku_mem_owner_t b)
{ return a.slot_plus_one == b.slot_plus_one && a.generation == b.generation; }
/** @brief Non-zero when two reservation handles are equal. */
static int backing_equal(tiku_mem_backing_t a, tiku_mem_backing_t b)
{ return a.slot_plus_one == b.slot_plus_one && a.generation == b.generation; }
/** @brief Slot of the live owner @p h names, or -1. */
static int owner_index(tiku_mem_owner_t h)
{
    unsigned i;
    if (!h.slot_plus_one || h.slot_plus_one > TIKU_MEM_MAX_OWNERS || !h.generation) return -1;
    i = h.slot_plus_one - 1u;
    return owners[i].live && owners[i].generation == h.generation ? (int)i : -1;
}
/** @brief Current handle of owner slot @p i. */
static tiku_mem_owner_t owner_handle(unsigned i)
{ return (tiku_mem_owner_t){owners[i].generation, (uint16_t)(i + 1)}; }
/** @brief Live ticket @p h names, or NULL. */
static ticket_t *ticket_get(tiku_mem_ticket_t h)
{
    ticket_t *t;
    if (!h.slot_plus_one || h.slot_plus_one > TIKU_MEM_MAX_TICKETS || !h.generation) return NULL;
    t = &tickets[h.slot_plus_one - 1u];
    return t->live && t->generation == h.generation ? t : NULL;
}
/** @brief Handle of the running job; slot 0 when none runs. */
static tiku_mem_job_t job_handle(void)
{ return (tiku_mem_job_t){job.generation, job.active ? 1u : 0u}; }
/** @brief Non-zero when @p h names the running job. */
static int job_equal(tiku_mem_job_t h)
{ return job.active && h.slot_plus_one == 1 && h.generation == job.generation; }
/** @brief Non-zero for a claimed, cancelled or failed ticket. */
static int terminal(const ticket_t *t)
{
    return t->status.state == TIKU_MEM_TICKET_CLAIMED ||
           t->status.state == TIKU_MEM_TICKET_CANCELLED ||
           t->status.state == TIKU_MEM_TICKET_FAILED;
}
/** @brief End @p t in @p state for @p cause; it is kept RETAIN_TICKS. */
static void finish(ticket_t *t, tiku_mem_ticket_state_t state,
                    tiku_mem_reclaim_cause_t cause)
{
    t->status.state = state; t->status.cause = cause;
    t->status.phase = TIKU_MEM_RECLAIM_NONE;
    t->status.deadline = tiku_clock_time() + RETAIN_TICKS;
}
/** @brief Non-zero once @p now has reached @p deadline. */
static int due(tiku_clock_time_t now, tiku_clock_time_t deadline)
{ return !TIKU_CLOCK_LT(now, deadline); }

/**
 * @brief Non-zero when @p n bytes at @p p lie outside every tier span.
 *
 * A control object in tier backing could be invalidated by this or a later
 * job, so controls must lie outside all registered backing spans.
 */
static int stable_control(const void *p, size_t n)
{
    unsigned tier;
    uintptr_t a = (uintptr_t)p;
    if (!p || !n || n > UINTPTR_MAX - a) return 0;
    for (tier = 0; tier < TIKU_MEM_TIER_COUNT; tier++) {
        unsigned si;
        for (si = 0; si < 256; si++) {
            const uint8_t *base; tiku_mem_stats_t s;
            uintptr_t b;
            if (tiku_tier_span_stats((tiku_mem_tier_t)tier, (uint8_t)si, &base, &s) != TIKU_MEM_OK) break;
            b = (uintptr_t)base;
            if ((a >= b && a - b < s.total_bytes) ||
                (a < b && b - a < n)) return 0;
        }
    }
    return 1;
}

/**
 * @brief Non-zero when an owner's context, process and INIT data all lie
 *        outside tier backing.
 */
static int owner_controls_stable(const tiku_mem_owner_registration_t *r)
{
    if ((r->context || r->context_size) && !stable_control(r->context, r->context_size)) return 0;
    if (r->process && (!stable_control(r->process, sizeof *r->process) ||
        (r->process->init_data ? !stable_control(r->process->init_data, r->init_data_size) :
                               r->init_data_size != 0))) return 0;
    return 1;
}

tiku_mem_err_t tiku_mem_owner_register(const tiku_mem_owner_registration_t *r,
                                       tiku_mem_owner_t *out)
{
    unsigned i, j;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!r || !out || !r->name || !r->step || !r->prepare_ticks || !r->recovery_ticks ||
        r->prepare_ticks > TIKU_CLOCK_MAX_INTERVAL || r->recovery_ticks > TIKU_CLOCK_MAX_INTERVAL ||
        (r->flags & ~TIKU_MEM_OWNER_NVM) || !owner_controls_stable(r))
        return TIKU_MEM_ERR_INVALID;
    if (r->process && (!r->process->is_running || !r->process->thread ||
        (r->process->restart != TIKU_RESTART_ALWAYS && r->process->restart != TIKU_RESTART_ON_FAILURE) ||
        !stable_control(r->process, sizeof *r->process) ||
        (r->process->init_data ? !stable_control(r->process->init_data, r->init_data_size) :
                               r->init_data_size != 0))) return TIKU_MEM_ERR_INVALID;
    if (!r->process && r->init_data_size) return TIKU_MEM_ERR_INVALID;
    for (i = 0; i < TIKU_MEM_MAX_OWNERS; i++)
        if (r->process && owners[i].live && owners[i].registration.process == r->process)
            return TIKU_MEM_ERR_BUSY;
    if (polling) return TIKU_MEM_ERR_BUSY;
    for (i = 0; i < TIKU_MEM_MAX_OWNERS; i++) {
        owner_t *o = &owners[i];
        if (o->live || o->generation == TIKU_MEM_GENERATION_MAX) continue;
        { uint32_t generation = o->generation + 1; *o = (owner_t){0}; o->generation = generation; }
        o->registration = *r;
        if (r->process) o->process_generation = r->process->generation;
        for (j = 0; j < sizeof o->name - 1u && r->name[j]; j++) o->name[j] = r->name[j];
        o->name[j] = 0; o->registration.name = o->name; o->live = 1;
        *out = owner_handle(i);
        return TIKU_MEM_OK;
    }
    return TIKU_MEM_ERR_FULL;
}

tiku_mem_err_t tiku_mem_owner_unregister(tiku_mem_owner_t h)
{
    unsigned i; int oi = owner_index(h);
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (oi < 0) return TIKU_MEM_ERR_INVALID;
    if (job.active && (job.owners & (1u << oi))) return TIKU_MEM_ERR_BUSY;
    for (i = 0; i < N; i++) {
        tiku_reclaim_record_t r;
        if (tiku_reclaim_record(i, &r) && owner_equal(h, r.owner)) return TIKU_MEM_ERR_BUSY;
    }
    for (i = 0; i < TIKU_MEM_MAX_TICKETS; i++)
        if (tickets[i].live && !terminal(&tickets[i]) &&
            (owner_equal(h, tickets[i].request.requester) ||
             owner_equal(h, tickets[i].request.options.owner))) return TIKU_MEM_ERR_BUSY;
    owners[oi].live = 0;
    return TIKU_MEM_OK;
}

int tiku_mem_owner_available(tiku_mem_owner_t h)
{
    int oi = owner_index(h);
    return oi >= 0 && !(job.active && job.credits && (job.owners & (1u << oi)));
}
int tiku_reclaim_owner_access(tiku_mem_owner_t h)
{
    int oi;
    if (!h.slot_plus_one && !h.generation) return 1;
    oi = owner_index(h);
    return oi >= 0 && (callback_owner == oi || process_owner_current((unsigned)oi) ||
                      tiku_mem_owner_available(h));
}

int tiku_reclaim_owner_options(const tiku_mem_request_t *r, uint8_t *cls)
{
    tiku_mem_owner_t h = r ? r->owner : (tiku_mem_owner_t){0};
    tiku_mem_class_t c = r ? r->allocation_class : TIKU_MEM_CLASS_DEFAULT;
    int owned = h.slot_plus_one || h.generation;
    if (owned && (owner_index(h) < 0 || !r->owner_slot)) return 0;
    if (!owned && r && r->owner_slot) return 0;
    if (c == TIKU_MEM_CLASS_DEFAULT) c = owned ? TIKU_MEM_RESTARTABLE : TIKU_MEM_TRANSIENT;
    if (owned ? c != TIKU_MEM_RESTARTABLE :
        (c != TIKU_MEM_TRANSIENT && c != TIKU_MEM_FIXED)) return 0;
    *cls = (uint8_t)c;
    return 1;
}

/** @brief Non-zero when @p owner uses @p key in a record or a live ticket. */
static int key_used(tiku_mem_owner_t owner, uint16_t key)
{
    unsigned i;
    if (!owner.slot_plus_one) return 0;
    for (i = 0; i < N; i++) {
        tiku_reclaim_record_t r;
        if (tiku_reclaim_record(i, &r) && owner_equal(owner, r.owner) && r.owner_slot == key) return 1;
    }
    for (i = 0; i < TIKU_MEM_MAX_TICKETS; i++)
        if (tickets[i].live && !terminal(&tickets[i]) &&
            owner_equal(owner, tickets[i].request.options.owner) &&
            tickets[i].request.options.owner_slot == key) return 1;
    return 0;
}

tiku_mem_err_t tiku_reclaim_owner_create(const tiku_mem_request_t *options,
    tiku_mem_tier_t tier, int span, uint8_t kind, tiku_mem_arch_size_t length,
    tiku_mem_arch_size_t alignment, tiku_mem_arch_size_t stride,
    tiku_mem_arch_size_t count, void *descriptor, uint8_t id, int *claimed)
{
    unsigned i; int oi;
    if (!options || !options->owner.slot_plus_one) return TIKU_MEM_OK;
    oi = owner_index(options->owner);
    if (oi < 0 || !stable_control(descriptor, kind == TIKU_BACKING_ARENA ?
                                  sizeof(tiku_arena_t) : sizeof(tiku_pool_t))) return TIKU_MEM_ERR_INVALID;
    if (tier == TIKU_MEM_NVM && !(owners[oi].registration.flags & TIKU_MEM_OWNER_NVM))
        return TIKU_MEM_ERR_INVALID;
    if (job.active && job.credits && (job.owners & (1u << oi))) {
        if ((callback_owner != oi && !process_owner_current((unsigned)oi)) || job.fault ||
            !((job.committed && tickets[job.ticket].status.phase == TIKU_MEM_RECLAIM_RESTORE) ||
              ((job.old_rebound & (1u << oi)) &&
               tickets[job.ticket].status.phase == TIKU_MEM_RECLAIM_ABORT))) return TIKU_MEM_ERR_BUSY;
        for (i = 0; i < job.count; i++) {
            const tiku_reclaim_record_t *r = &job.layout[i];
            tiku_mem_err_t err;
            if (!owner_equal(options->owner, r->owner) || options->owner_slot != r->owner_slot) continue;
            if (r->kind != kind || r->length != length || r->alignment != alignment ||
                r->block_size != stride || r->block_count != count ||
                (tier != TIKU_MEM_AUTO && tier != r->tier) ||
                (span >= 0 && span != r->span_index) ||
                (tier == TIKU_MEM_AUTO && (r->tier == TIKU_MEM_NVM ||
                 (r->tier == TIKU_MEM_PSRAM && !(options->flags & TIKU_MEM_ALLOW_EXTERNAL)))))
                return TIKU_MEM_ERR_INVALID;
            *claimed = 1;
            err = tiku_reclaim_bind(r->handle, descriptor, id, 1);
            if (err == TIKU_MEM_OK) counters.restored_slots++;
            return err;
        }
        return TIKU_MEM_ERR_INVALID;
    }
    return key_used(options->owner, options->owner_slot) ? TIKU_MEM_ERR_BUSY : TIKU_MEM_OK;
}

/** @brief Non-zero when two records lie in the same span. */
static int same_span(const tiku_reclaim_record_t *a, const tiku_reclaim_record_t *b)
{ return a->tier == b->tier && a->span_index == b->span_index; }
/** @brief Non-zero when two records overlap in one span. */
static int overlap(const tiku_reclaim_record_t *a, const tiku_reclaim_record_t *b)
{
    return same_span(a, b) && a->offset < b->offset + b->length &&
           b->offset < a->offset + a->length;
}
/** @brief Non-zero for a live record of an owner the job selected. */
static int selected(const tiku_reclaim_record_t *r)
{
    int oi = owner_index(r->owner);
    return r->state == 1 && oi >= 0 && (job.owners & (1u << oi));
}
/**
 * @brief End of the highest FIXED reservation in @p object's span, below
 *        which nothing is placed.
 */
static tiku_mem_arch_size_t lower_boundary(const tiku_reclaim_record_t *object)
{
    tiku_mem_arch_size_t low = 0;
    unsigned i;
    for (i = 0; i < N; i++) {
        const tiku_reclaim_record_t *r = &job.snapshot[i];
        if (r->state && same_span(r, object) && r->allocation_class == TIKU_MEM_FIXED &&
            r->offset + r->length > low) low = r->offset + r->length;
    }
    return low;
}

/**
 * @brief Find the @p ordinal-th candidate offset for layout entry @p object.
 *
 * Candidates are the lowest and highest aligned offsets of each free
 * interval; every call rescans all intervals, gaps left by alignment too.
 *
 * @return 1 with @p offset set, or 0 past the last candidate
 */
static int endpoint(unsigned object, unsigned ordinal, tiku_mem_arch_size_t *offset)
{
    tiku_reclaim_record_t *r = &job.layout[object];
    const uint8_t *base; tiku_mem_stats_t stats;
    tiku_mem_arch_size_t at = lower_boundary(r);
    unsigned found = 0, loops;
    uintptr_t b, mask = r->alignment - 1u;
    if (tiku_tier_span_stats(r->tier, r->span_index, &base, &stats) != TIKU_MEM_OK) return 0;
    b = (uintptr_t)base;
    for (loops = 0; loops <= 2u * N + 1u; loops++) {
        tiku_mem_arch_size_t end = stats.total_bytes, next = stats.total_bytes;
        unsigned i;
        for (i = 0; i < N; i++) {
            const tiku_reclaim_record_t *o = &job.snapshot[i];
            if (o->state && !selected(o) && same_span(o, r) && o->offset >= at && o->offset < end) {
                end = o->offset; next = o->offset + o->length;
            }
        }
        for (i = 0; i <= job.count; i++) {
            const tiku_reclaim_record_t *o = &job.layout[i];
            if (job.placed[i] && same_span(o, r) && o->offset >= at && o->offset < end) {
                end = o->offset; next = o->offset + o->length;
            }
        }
        if (end >= at && r->length <= end - at && b + at <= UINTPTR_MAX - mask) {
            uintptr_t low = (b + at + mask) & ~mask;
            uintptr_t high = (b + end - r->length) & ~mask;
            if (low >= b + at && low <= high) {
                if (found++ == ordinal) { *offset = (tiku_mem_arch_size_t)(low - b); return 1; }
                if (high != low && found++ == ordinal) { *offset = (tiku_mem_arch_size_t)(high - b); return 1; }
            }
        }
        if (end == stats.total_bytes || next <= at) break;
        at = next;
    }
    return 0;
}
/** @brief Place entry @p object at its highest (@p high) or lowest offset. */
static int place_end(unsigned object, int high)
{
    tiku_mem_arch_size_t at, chosen = 0;
    unsigned ordinal = 0;
    int found = 0;
    while (endpoint(object, ordinal++, &at)) {
        if (!found || (high ? at > chosen : at < chosen)) chosen = at;
        found = 1;
    }
    if (found) { job.layout[object].offset = chosen; job.placed[object] = 1; }
    return found;
}

/**
 * @brief Non-zero when the whole layout is consistent.
 *
 * Each entry is placed, aligned, inside its span and above the FIXED boundary,
 * clear of every other record, and a moved one matches its old record.  The
 * check is independent of the enumeration and search that built the layout.
 */
static int layout_valid(void)
{
    unsigned i, j;
    for (i = 0; i <= job.count; i++) {
        const tiku_reclaim_record_t *r = &job.layout[i];
        const uint8_t *base; tiku_mem_stats_t s;
        if (!job.placed[i] || tiku_tier_span_stats(r->tier, r->span_index, &base, &s) != TIKU_MEM_OK ||
            !r->length || r->offset > s.total_bytes || r->length > s.total_bytes - r->offset ||
            (((uintptr_t)base + r->offset) & (r->alignment - 1u)) || r->offset < lower_boundary(r)) return 0;
        if (i < job.count) {
            const tiku_reclaim_record_t *o = &job.old[i];
            if (!same_span(r, o) || r->length != o->length || r->alignment != o->alignment ||
                r->kind != o->kind || !owner_equal(r->owner, o->owner) || r->owner_slot != o->owner_slot ||
                r->block_size != o->block_size || r->block_count != o->block_count) return 0;
        }
        for (j = i + 1; j <= job.count; j++) if (overlap(r, &job.layout[j])) return 0;
        for (j = 0; j < N; j++) {
            const tiku_reclaim_record_t *o = &job.snapshot[j];
            if (o->state && !selected(o) && overlap(r, o)) return 0;
        }
    }
    return 1;
}

/**
 * @brief Non-zero when the reservation table still matches the snapshot.
 *
 * Once the job holds credits, records outside the fenced spans may change.
 */
static int snapshot_unchanged(void)
{
    unsigned i;
    for (i = 0; i < N; i++) {
        tiku_reclaim_record_t now = {0};
        const tiku_reclaim_record_t *was = &job.snapshot[i];
        (void)tiku_reclaim_record(i, &now);
        if (job.credits &&
            (!now.state || !tiku_reclaim_fenced(now.tier, now.span_index)) &&
            (!was->state || !tiku_reclaim_fenced(was->tier, was->span_index))) continue;
        if (now.state != was->state || (now.state &&
            (!backing_equal(now.handle, was->handle) || now.descriptor != was->descriptor ||
             !same_span(&now, was) || now.offset != was->offset || now.length != was->length ||
             !owner_equal(now.owner, was->owner) || now.owner_slot != was->owner_slot))) return 0;
    }
    return 1;
}
/** @brief Clear the fence on every span the layout uses. */
static void release_fences(void)
{
    unsigned i;
    for (i = 0; i <= job.count; i++)
        (void)tiku_reclaim_fence(job.layout[i].tier, job.layout[i].span_index, 0);
}
/** @brief Drop the credits the job holds. */
static void release_credits(void)
{
    unsigned i;
    for (i = 0; i < job.credits; i++) tiku_reclaim_drop(job.layout[i].handle);
    job.credits = 0;
}
/** @brief End the job and settle its ticket: READY, CANCELLED or FAILED. */
static void end_job(tiku_mem_reclaim_cause_t cause)
{
    ticket_t *t = &tickets[job.ticket];
    release_fences();
    if (!job.committed) release_credits();
    if (t->cancelling) {
        tiku_reclaim_drop(t->result.handle);
        finish(t, TIKU_MEM_TICKET_CANCELLED, cause);
        counters.cancelled++;
    } else if (cause != TIKU_MEM_RECLAIM_OK) {
        finish(t, TIKU_MEM_TICKET_FAILED, cause);
        counters.refused++;
    } else {
        t->status.state = TIKU_MEM_TICKET_READY;
        t->status.phase = TIKU_MEM_RECLAIM_NONE;
        t->status.cause = TIKU_MEM_RECLAIM_OK;
        t->status.deadline = tiku_clock_time() + CLAIM_TICKS;
        counters.completed++;
    }
    last_job.generation = job.generation; last_job.steps = job.steps;
    last_job.state = t->status.state; last_job.cause = t->status.cause;
    job.active = 0;
}
/** @brief Fault the job; it makes no progress until a retry. */
static void fault(tiku_mem_reclaim_cause_t cause)
{
    ticket_t *t = &tickets[job.ticket];
    job.fault = 1; t->status.state = TIKU_MEM_TICKET_FAULT; t->status.cause = cause;
    counters.faults++;
}
/** @brief Set the ticket deadline to the earliest pending owner's. */
static void phase_deadline(void)
{
    ticket_t *t = &tickets[job.ticket];
    uint16_t required = t->status.phase == TIKU_MEM_RECLAIM_ABORT ? job.touched : job.owners;
    unsigned i; int found = 0;
    for (i = 0; i < TIKU_MEM_MAX_OWNERS; i++) {
        if (!(required & (1u << i)) || (job.done & (1u << i))) continue;
        if (!found || TIKU_CLOCK_LT(job.deadline[i], t->status.deadline))
            t->status.deadline = job.deadline[i];
        found = 1;
    }
    if (!found) t->status.deadline = tiku_clock_time();
}
/** @brief Enter @p phase: clear owner progress and start the deadlines. */
static void phase_begin(tiku_mem_reclaim_phase_t phase)
{
    unsigned i; tiku_clock_time_t now = tiku_clock_time();
    tickets[job.ticket].status.phase = phase;
    job.done = 0; job.turn = 0;
    for (i = 0; i < TIKU_MEM_MAX_OWNERS; i++) {
        tiku_clock_time_t interval = phase == TIKU_MEM_RECLAIM_PREPARE ?
            owners[i].registration.prepare_ticks : owners[i].registration.recovery_ticks;
        job.deadline[i] = now + interval;
        owners[i].consent = 0;
    }
    phase_deadline();
}
/** @brief Abandon the job for @p cause; the touched owners get ABORT. */
static void abort_job(tiku_mem_reclaim_cause_t cause)
{
    tickets[job.ticket].status.cause = cause;
    phase_begin(TIKU_MEM_RECLAIM_ABORT);
}

/**
 * @brief Recheck the plan, take credits, fence the spans and begin PREPARE.
 * @return 1 when the job moved on (to PREPARE, or ended for lack of
 *         records); 0 when the plan fails its recheck, a selected owner's
 *         controls are in tier backing, or a selected process owner has
 *         events queued
 */
static int freeze(void)
{
    unsigned i;
    if (!layout_valid() || !snapshot_unchanged()) return 0;
    for (i = 0; i < TIKU_MEM_MAX_OWNERS; i++)
        if ((job.owners & (1u << i)) && !owner_controls_stable(&owners[i].registration)) return 0;
    /* No admitted ordinary event may be discarded before preparation. Close
     * the IRQ-post race while checking the queue and publishing the gates. */
    tiku_atomic_enter();
    for (i = 0; i < TIKU_MEM_MAX_OWNERS; i++)
        if ((job.owners & (1u << i)) && owners[i].registration.process &&
            !tiku_process_reclaim_quiet(owners[i].registration.process)) {
            tiku_atomic_exit(); return 0;
        }
    job.credits = 0;
    for (i = 0; i <= job.count; i++) {
        if (!tiku_reclaim_credit(&job.layout[i].handle)) {
            release_credits(); end_job(TIKU_MEM_RECLAIM_RECORDS);
            tiku_atomic_exit(); return 1;
        }
        job.credits++;
    }
    for (i = 0; i < TIKU_MEM_MAX_OWNERS; i++) if (job.owners & (1u << i)) {
        owners[i].stopped = 0; owners[i].init_failed = 0; owners[i].starting = 0;
    }
    for (i = 0; i <= job.count; i++) {
        (void)tiku_reclaim_fence(job.layout[i].tier, job.layout[i].span_index, 1);
        job.layout[i].descriptor = NULL;
    }
    if (job.fast_path == 1) counters.layout_low++;
    else if (job.fast_path == 2) counters.layout_high++;
    else counters.searched++;
    phase_begin(TIKU_MEM_RECLAIM_PREPARE);
    tiku_atomic_exit();
    return 1;
}

/**
 * @brief Take a fresh snapshot and plan again after the span moved.
 *
 * No owner has been asked to stop, so planning can start over, at most
 * TIKU_MEM_RECLAIM_PLAN_ATTEMPTS times per job.
 *
 * @return 1 when planning starts over, 0 when the attempts are spent
 */
static int replan(void)
{
    ticket_t *t = &tickets[job.ticket];
    unsigned i;
    if (job.replans >= TIKU_MEM_RECLAIM_PLAN_ATTEMPTS) return 0;
    job.replans++; counters.replanned++;
    for (i = 0; i < N; i++) (void)tiku_reclaim_record(i, &job.snapshot[i]);
    job.tier_cursor = 0; job.boundary = 0; job.owners = 0; job.count = 0;
    job.searching = 0; job.depth = 0; job.fast_path = 0;
    job.span_cursor = t->request.placement == TIKU_MEM_PLACE_SPAN ?
                      t->request.span_index : 0;
    return 1;
}

/**
 * @brief Place the request lowest (or highest, @p high), then each moved
 *        record as high as it fits, in old address order.
 * @return Non-zero when that gives a valid layout
 */
static int fast_layout(int high)
{
    unsigned i, j;
    for (i = 0; i <= job.count; i++) { job.placed[i] = 0; job.order[i] = (uint16_t)i; }
    for (i = 1; i < job.count; i++) {
        uint16_t x = job.order[i]; j = i;
        while (j && job.old[job.order[j - 1]].offset > job.old[x].offset) {
            job.order[j] = job.order[j - 1]; j--;
        }
        job.order[j] = x;
    }
    if (!place_end(job.count, high)) return 0;
    for (i = 0; i < job.count; i++) if (!place_end(job.order[i], 1)) return 0;
    job.fast_path = (uint8_t)(high ? 2 : 1);
    return layout_valid();
}
/** @brief Start the layout search, most aligned and then largest first. */
static void search_start(void)
{
    unsigned i, j;
    for (i = 0; i <= job.count; i++) {
        job.placed[i] = 0; job.order[i] = (uint16_t)i; job.choice[i] = 0;
    }
    for (i = 1; i <= job.count; i++) {
        uint16_t x = job.order[i]; j = i;
        while (j) {
            const tiku_reclaim_record_t *a = &job.layout[job.order[j - 1]], *b = &job.layout[x];
            if (a->alignment > b->alignment ||
                (a->alignment == b->alignment && a->length >= b->length)) break;
            job.order[j] = job.order[j - 1]; j--;
        }
        job.order[j] = x;
    }
    job.searching = 1; job.depth = 0; job.fast_path = 0;
}

/**
 * @brief Tier to try at tier cursor @p tc and span @p si for ticket @p t.
 * @return 1 with @p tier set, or 0 when that cursor or span is not eligible
 */
static int eligible_span(const ticket_t *t, uint8_t tc, uint8_t si, tiku_mem_tier_t *tier)
{
    if (t->request.placement != TIKU_MEM_PLACE_WORKING) {
        if (tc || (t->request.placement == TIKU_MEM_PLACE_SPAN && si != t->request.span_index)) return 0;
        *tier = t->request.tier;
    } else {
        tiku_mem_tier_t order[3] = {TIKU_MEM_SRAM, TIKU_MEM_HIFRAM, TIKU_MEM_PSRAM};
#if TIKU_TIER_AUTO_HIFRAM_THRESHOLD > 0
        if (t->request.size >= TIKU_TIER_AUTO_HIFRAM_THRESHOLD) {
            order[0] = TIKU_MEM_HIFRAM; order[1] = TIKU_MEM_SRAM;
        }
#endif
        if (tc > 2 || (tc == 2 && !(t->request.options.flags & TIKU_MEM_ALLOW_EXTERNAL))) return 0;
        *tier = order[tc];
    }
    return 1;
}

/**
 * @brief Try one candidate per poll: one span's suffix from a boundary up.
 *
 * Every reservation in the suffix must be live and restartable under an
 * eligible owner, so transient ones stay obstacles; selecting an owner takes
 * its reservations in every span.
 *
 * @return 1 when a fast layout was found, 0 to go on planning, -1 when no
 *         span is left
 */
static int candidate(void)
{
    ticket_t *t = &tickets[job.ticket];
    const uint8_t *base; tiku_mem_stats_t s;
    tiku_mem_tier_t tier;
    tiku_mem_arch_size_t start;
    tiku_reclaim_record_t *request;
    unsigned i;
    if (job.tier_cursor > 2) return -1;
    if (!eligible_span(t, job.tier_cursor, job.span_cursor, &tier) ||
        tiku_tier_span_stats(tier, job.span_cursor, &base, &s) != TIKU_MEM_OK) {
        job.tier_cursor++; job.span_cursor = 0; job.boundary = 0; return 0;
    }
    if (job.boundary > N) {
        job.boundary = 0;
        if (t->request.placement == TIKU_MEM_PLACE_SPAN || job.span_cursor == 255) {
            job.tier_cursor++; job.span_cursor = 0;
        } else job.span_cursor++;
        return 0;
    }
    i = job.boundary++;
    if (!i) start = 0;
    else {
        const tiku_reclaim_record_t *r = &job.snapshot[i - 1];
        if (!r->state || r->tier != tier || r->span_index != job.span_cursor) return 0;
        start = r->offset + r->length;
    }
    if (start >= s.total_bytes || t->result.length > s.total_bytes - s.used_bytes) return 0;
    job.owners = 0;
    for (i = 0; i < N; i++) {
        const tiku_reclaim_record_t *r = &job.snapshot[i]; int oi;
        if (!r->state || r->tier != tier || r->span_index != job.span_cursor || r->offset < start) continue;
        oi = owner_index(r->owner);
        if (r->state != 1 || r->allocation_class != TIKU_MEM_RESTARTABLE || oi < 0 ||
            owner_equal(r->owner, t->request.requester) ||
            owner_equal(r->owner, t->request.options.owner)) return 0;
        if (owners[oi].registration.process) {
            const struct tiku_process *p = owners[oi].registration.process;
            if (!p->is_running || p == t->process || p->generation != owners[oi].process_generation ||
                (p->restart != TIKU_RESTART_ALWAYS && p->restart != TIKU_RESTART_ON_FAILURE) ||
                (p->init_data ? !stable_control(p->init_data, owners[oi].registration.init_data_size) :
                                owners[oi].registration.init_data_size != 0)) return 0;
        }
        job.owners |= (uint16_t)(1u << oi);
    }
    if (!job.owners) return 0;
    /* A pending result owned by one of these components is not part of its
     * current live manifest. Do not reconstruct it underneath that request. */
    for (i = 0; i < N; i++) {
        const tiku_reclaim_record_t *r = &job.snapshot[i];
        int oi = owner_index(r->owner);
        if (r->state && oi >= 0 && (job.owners & (1u << oi)) && r->state != 1) return 0;
    }
    for (i = 0; i < TIKU_MEM_MAX_TICKETS; i++) {
        int oi = owner_index(tickets[i].request.options.owner);
        if (tickets[i].live && !terminal(&tickets[i]) && oi >= 0 &&
            (job.owners & (1u << oi))) return 0;
    }
    job.count = 0;
    for (i = 0; i < N; i++) {
        const tiku_reclaim_record_t *r = &job.snapshot[i];
        if (!selected(r)) continue;
        if (!stable_control(r->descriptor, r->kind == TIKU_BACKING_ARENA ?
                             sizeof(tiku_arena_t) : sizeof(tiku_pool_t))) return 0;
        job.old[job.count] = *r; job.layout[job.count++] = *r;
    }
    request = &job.layout[job.count];
    *request = t->result; request->tier = tier; request->span_index = job.span_cursor;
    if (fast_layout(0) || fast_layout(1)) {
        if (!freeze() && !replan()) end_job(TIKU_MEM_RECLAIM_CHANGED);
        return 1;
    }
    search_start();
    return 0;
}

/**
 * @brief One planning step: try a candidate, or run up to
 *        TIKU_MEM_RECLAIM_STEPS search steps.
 */
static void plan_step(void)
{
    unsigned work;
    if (!snapshot_unchanged()) {
        if (!replan()) end_job(TIKU_MEM_RECLAIM_CHANGED);
        return;
    }
    if (!job.searching) {
        if (candidate() < 0) end_job(TIKU_MEM_RECLAIM_NO_LAYOUT);
        return;
    }
    for (work = 0; work < TIKU_MEM_RECLAIM_STEPS; work++) {
        tiku_mem_arch_size_t at; unsigned object;
        if (++job.steps > TIKU_MEM_RECLAIM_SEARCH_LIMIT) {
            counters.search_limited++; end_job(TIKU_MEM_RECLAIM_SEARCH_LIMITED); return;
        }
        if (job.depth > job.count) {
            if (!freeze() && !replan()) end_job(TIKU_MEM_RECLAIM_CHANGED);
            return;
        }
        object = job.order[job.depth];
        job.placed[object] = 0;
        if (endpoint(object, job.choice[job.depth]++, &at)) {
            job.layout[object].offset = at; job.placed[object] = 1;
            job.depth++;
            if (job.depth <= job.count) job.choice[job.depth] = 0;
        } else if (job.depth) {
            job.depth--; job.placed[job.order[job.depth]] = 0;
        } else { job.searching = 0; return; }
    }
}

/** @brief Non-zero when every layout record of owner @p oi is live again. */
static int owner_restored(unsigned oi)
{
    unsigned i;
    for (i = 0; i < job.count; i++) {
        tiku_reclaim_record_t r;
        if (!owner_equal(job.layout[i].owner, owner_handle(oi))) continue;
        if (!tiku_reclaim_record(job.layout[i].handle.slot_plus_one - 1u, &r) || r.state != 1 ||
            !backing_equal(r.handle, job.layout[i].handle)) return 0;
    }
    return 1;
}
/** @brief Move PREPARE, RESTORE or ABORT on by one owner step. */
static void lifecycle_step(void)
{
    ticket_t *t = &tickets[job.ticket];
    tiku_mem_reclaim_phase_t phase = t->status.phase;
    uint16_t required = phase == TIKU_MEM_RECLAIM_ABORT ? job.touched : job.owners;
    unsigned i;
    if ((job.done & required) == required) {
        if (phase == TIKU_MEM_RECLAIM_PREPARE) {
            t->status.phase = TIKU_MEM_RECLAIM_COMMIT;
        } else end_job(phase == TIKU_MEM_RECLAIM_ABORT ? t->status.cause :
                     (t->cancelling ? TIKU_MEM_RECLAIM_CANCELLED : TIKU_MEM_RECLAIM_OK));
        return;
    }
    for (i = 0; i < TIKU_MEM_MAX_OWNERS; i++) {
        unsigned oi = job.turn++ % TIKU_MEM_MAX_OWNERS;
        uint16_t bit = (uint16_t)(1u << oi);
        tiku_mem_owner_result_t result;
        if (!(required & bit) || (job.done & bit)) continue;
        if (due(tiku_clock_time(), job.deadline[oi])) {
            if (phase == TIKU_MEM_RECLAIM_PREPARE) abort_job(TIKU_MEM_RECLAIM_TIMEOUT);
            else fault(TIKU_MEM_RECLAIM_TIMEOUT);
            return;
        }
        job.touched |= bit;
        if (owners[oi].registration.process) result = process_step(oi);
        else {
            callback_owner = (int)oi;
            result = owners[oi].registration.step(owners[oi].registration.context, job_handle(),
                phase == TIKU_MEM_RECLAIM_PREPARE ? TIKU_MEM_OWNER_PREPARE :
                phase == TIKU_MEM_RECLAIM_ABORT ? TIKU_MEM_OWNER_ABORT : TIKU_MEM_OWNER_RESTORE);
            callback_owner = -1;
        }
        /* A callback may request cancellation. Never accept old-phase DONE. */
        if (t->status.phase != phase || job.fault) return;
        if (result == TIKU_MEM_OWNER_DONE) {
            if (phase == TIKU_MEM_RECLAIM_RESTORE && !owner_restored(oi)) fault(TIKU_MEM_RECLAIM_OWNER_FAULT);
            else { job.done |= bit; phase_deadline(); }
        } else if (result != TIKU_MEM_OWNER_WAIT) {
            if (phase == TIKU_MEM_RECLAIM_PREPARE)
                abort_job(result == TIKU_MEM_OWNER_BUSY ? TIKU_MEM_RECLAIM_REFUSED : TIKU_MEM_RECLAIM_OWNER_FAULT);
            else fault(TIKU_MEM_RECLAIM_OWNER_FAULT);
        }
        return;
    }
}

/** @brief Copy the result's placement into the ticket status. */
static void result_status(ticket_t *t)
{
    t->status.tier = t->result.tier; t->status.span_index = t->result.span_index;
    t->status.offset = t->result.offset; t->status.length = t->result.length;
}
/**
 * @brief Start ticket @p index: hold its request directly when it fits, or
 *        start a job when an eligible span has enough free bytes in total.
 */
static void admit(unsigned index)
{
    unsigned i;
    ticket_t *t = &tickets[index];
    tiku_mem_err_t err = tiku_reclaim_hold_direct(&t->result, &t->request);
    if (err == TIKU_MEM_OK) {
        t->status.state = TIKU_MEM_TICKET_READY;
        t->status.phase = TIKU_MEM_RECLAIM_NONE;
        t->status.deadline = tiku_clock_time() + CLAIM_TICKS;
        result_status(t); counters.direct++; return;
    }
    if (err == TIKU_MEM_ERR_FULL) { finish(t, TIKU_MEM_TICKET_FAILED, TIKU_MEM_RECLAIM_RECORDS); return; }
    {
        unsigned tc, si; int capacity = 0;
        for (tc = 0; tc < 3; tc++) for (si = 0; si < 256; si++) {
            tiku_mem_tier_t tier; tiku_mem_space_t space;
            uint8_t span = t->request.placement == TIKU_MEM_PLACE_SPAN ? t->request.span_index : (uint8_t)si;
            if (!eligible_span(t, (uint8_t)tc, span, &tier) ||
                tiku_tier_span_space(tier, span, &space) != TIKU_MEM_OK) break;
            if (space.free_bytes >= t->result.length) capacity = 1;
            if (t->request.placement == TIKU_MEM_PLACE_SPAN) break;
        }
        if (!capacity) { finish(t, TIKU_MEM_TICKET_FAILED, TIKU_MEM_RECLAIM_CAPACITY); return; }
    }
    if (job.generation == TIKU_MEM_GENERATION_MAX) {
        finish(t, TIKU_MEM_TICKET_FAILED, TIKU_MEM_RECLAIM_RECORDS); return;
    }
    if (t->result.allocation_class == TIKU_MEM_FIXED) {
        finish(t, TIKU_MEM_TICKET_FAILED, TIKU_MEM_RECLAIM_NO_LAYOUT); return;
    }
    {
        uint32_t generation = job.generation + 1;
        job = (job_t){0}; job.generation = generation;
    }
    job.active = 1; job.ticket = (uint8_t)index;
    if (t->request.placement == TIKU_MEM_PLACE_SPAN) job.span_cursor = t->request.span_index;
    for (i = 0; i < N; i++) (void)tiku_reclaim_record(i, &job.snapshot[i]);
    t->status.phase = TIKU_MEM_RECLAIM_PLAN;
    t->status.job = job_handle();
}

tiku_mem_err_t tiku_mem_reclaim_submit(const tiku_mem_reclaim_request_t *request,
                                       tiku_mem_ticket_t *out)
{
    tiku_reclaim_record_t normalized;
    tiku_mem_err_t err; unsigned i;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!request || !out) return TIKU_MEM_ERR_INVALID;
    if (!enabled || polling) return TIKU_MEM_ERR_BUSY;
    err = tiku_reclaim_normalize(request, &normalized);
    if (err != TIKU_MEM_OK) return err;
    if ((request->requester.slot_plus_one || request->requester.generation) &&
        owner_index(request->requester) < 0) return TIKU_MEM_ERR_INVALID;
    if (request->requester.slot_plus_one && !tiku_mem_owner_available(request->requester))
        return TIKU_MEM_ERR_BUSY;
    if (key_used(normalized.owner, normalized.owner_slot)) return TIKU_MEM_ERR_BUSY;
    if (normalized.owner.slot_plus_one) {
        int oi = owner_index(normalized.owner);
        if (!tiku_mem_owner_available(normalized.owner)) return TIKU_MEM_ERR_BUSY;
        if (request->placement != TIKU_MEM_PLACE_WORKING && request->tier == TIKU_MEM_NVM &&
            !(owners[oi].registration.flags & TIKU_MEM_OWNER_NVM)) return TIKU_MEM_ERR_INVALID;
    }
    if (sequence == UINT32_MAX) return TIKU_MEM_ERR_FULL;
    for (i = 0; i < TIKU_MEM_MAX_TICKETS; i++) {
        ticket_t *t = &tickets[i];
        uint32_t generation;
        if (t->live || t->generation == TIKU_MEM_GENERATION_MAX) continue;
        generation = t->generation + 1;
        *t = (ticket_t){0}; t->generation = generation; t->live = 1;
        t->sequence = ++sequence; t->request = *request; t->result = normalized;
        t->status.state = TIKU_MEM_TICKET_PENDING;
        t->status.deadline = tiku_clock_time() + RETAIN_TICKS;
        t->process = TIKU_THIS();
        if (t->process) t->process_generation = t->process->generation;
        *out = (tiku_mem_ticket_t){generation, (uint16_t)(i + 1)};
        /* A direct fit becomes READY at once; any other ticket waits for
         * tiku_mem_reclaim_poll().  The ticket keeps no descriptor address. */
        if (!job.active) {
            unsigned prior;
            for (prior = 0; prior < TIKU_MEM_MAX_TICKETS; prior++)
                if (prior != i && tickets[prior].live && tickets[prior].status.state == TIKU_MEM_TICKET_PENDING)
                    break;
            if (prior == TIKU_MEM_MAX_TICKETS) admit(i);
        }
        return TIKU_MEM_OK;
    }
    return TIKU_MEM_ERR_FULL;
}

tiku_mem_err_t tiku_mem_reclaim_status(tiku_mem_ticket_t h, tiku_mem_reclaim_status_t *out)
{
    ticket_t *t = ticket_get(h);
    if (!t || !out) return TIKU_MEM_ERR_INVALID;
    *out = t->status;
    return TIKU_MEM_OK;
}
/**
 * @brief Non-zero when the caller is the process instance that submitted
 *        @p t, or kernel context for a ticket submitted there.
 */
static int caller_matches(const ticket_t *t)
{
    return t->process == TIKU_THIS() &&
           (!t->process || (t->process->is_running && t->process->generation == t->process_generation));
}
/** @brief Bind a READY ticket's result to @p descriptor of @p kind. */
static tiku_mem_err_t claim(tiku_mem_ticket_t h, void *descriptor, uint8_t kind)
{
    ticket_t *t = ticket_get(h); tiku_mem_err_t err;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!t || t->request.kind != kind || !caller_matches(t) || !descriptor) return TIKU_MEM_ERR_INVALID;
    if (t->status.state != TIKU_MEM_TICKET_READY) return TIKU_MEM_ERR_BUSY;
    if (tiku_reclaim_fenced(t->result.tier, t->result.span_index)) return TIKU_MEM_ERR_BUSY;
    if (due(tiku_clock_time(), t->status.deadline)) {
        tiku_reclaim_drop(t->result.handle);
        finish(t, TIKU_MEM_TICKET_CANCELLED, TIKU_MEM_RECLAIM_EXPIRED);
        counters.cancelled++;
        return TIKU_MEM_ERR_INVALID;
    }
    if (t->result.owner.slot_plus_one && !stable_control(descriptor,
        kind == TIKU_BACKING_ARENA ? sizeof(tiku_arena_t) : sizeof(tiku_pool_t))) return TIKU_MEM_ERR_INVALID;
    err = tiku_reclaim_bind(t->result.handle, descriptor, 0, 0);
    if (err == TIKU_MEM_OK) finish(t, TIKU_MEM_TICKET_CLAIMED, TIKU_MEM_RECLAIM_OK);
    return err;
}
tiku_mem_err_t tiku_mem_reclaim_claim_arena(tiku_mem_ticket_t h, tiku_arena_t *a)
{ return claim(h, a, TIKU_BACKING_ARENA); }
tiku_mem_err_t tiku_mem_reclaim_claim_pool(tiku_mem_ticket_t h, tiku_pool_t *p)
{ return claim(h, p, TIKU_BACKING_POOL); }

/** @brief Cancel @p t for @p cause: drop its result or abandon its job. */
static void cancel(ticket_t *t, tiku_mem_reclaim_cause_t cause)
{
    if (terminal(t)) return;
    t->cancelling = 1;
    if (job.active && t == &tickets[job.ticket]) {
        if (job.committed) tiku_reclaim_drop(t->result.handle);
        else if (t->status.phase == TIKU_MEM_RECLAIM_PLAN) end_job(cause);
        else if (!job.fault && t->status.phase != TIKU_MEM_RECLAIM_ABORT) abort_job(cause);
    } else {
        tiku_reclaim_drop(t->result.handle);
        finish(t, TIKU_MEM_TICKET_CANCELLED, cause);
        counters.cancelled++;
    }
}
tiku_mem_err_t tiku_mem_reclaim_cancel(tiku_mem_ticket_t h)
{
    ticket_t *t = ticket_get(h);
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!t || !caller_matches(t)) return TIKU_MEM_ERR_INVALID;
    if (t->status.state == TIKU_MEM_TICKET_CLAIMED) return TIKU_MEM_ERR_BUSY;
    cancel(t, TIKU_MEM_RECLAIM_CANCELLED);
    return TIKU_MEM_OK;
}
tiku_mem_err_t tiku_mem_reclaim_forget(tiku_mem_ticket_t h)
{
    ticket_t *t = ticket_get(h);
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!t || !caller_matches(t)) return TIKU_MEM_ERR_INVALID;
    if (!terminal(t)) return TIKU_MEM_ERR_BUSY;
    t->live = 0;
    return TIKU_MEM_OK;
}
tiku_mem_err_t tiku_mem_reclaim_retry(tiku_mem_job_t h)
{
    unsigned i;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!job_equal(h) || !job.fault) return TIKU_MEM_ERR_INVALID;
    if (tickets[job.ticket].status.cause == TIKU_MEM_RECLAIM_DETACHED) return TIKU_MEM_ERR_IO;
    job.fault = 0; tickets[job.ticket].status.state = TIKU_MEM_TICKET_PENDING;
    for (i = 0; i < TIKU_MEM_MAX_OWNERS; i++)
        job.deadline[i] = tiku_clock_time() + owners[i].registration.recovery_ticks;
    phase_deadline();
    return TIKU_MEM_OK;
}
tiku_mem_err_t tiku_mem_reclaim_enable(int value)
{
    unsigned i;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (value != 0 && value != 1) return TIKU_MEM_ERR_INVALID;
    enabled = (uint8_t)value;
    if (!enabled) for (i = 0; i < TIKU_MEM_MAX_TICKETS; i++)
        if (tickets[i].live) cancel(&tickets[i], TIKU_MEM_RECLAIM_DISABLED);
    return TIKU_MEM_OK;
}
void tiku_mem_reclaim_stats(tiku_mem_reclaim_stats_t *out)
{
    if (!out) return;
    *out = counters;
    out->metadata_bytes = sizeof owners + sizeof tickets + sizeof job + sizeof counters +
                          sizeof sequence + sizeof enabled + sizeof polling + sizeof callback_owner + sizeof last_job;
}

void tiku_reclaim_detached(tiku_mem_tier_t tier, uint8_t si)
{
    unsigned i;
    if (job.active) for (i = 0; i <= job.count; i++)
        if (job.layout[i].tier == tier && job.layout[i].span_index == si && job.credits) {
            fault(TIKU_MEM_RECLAIM_DETACHED); break;
        }
    for (i = 0; i < TIKU_MEM_MAX_TICKETS; i++) {
        ticket_t *t = &tickets[i];
        if (t->live && t->status.state == TIKU_MEM_TICKET_READY &&
            t->result.tier == tier && t->result.span_index == si) {
            tiku_reclaim_drop(t->result.handle);
            finish(t, TIKU_MEM_TICKET_FAILED, TIKU_MEM_RECLAIM_DETACHED);
        }
    }
}

int tiku_mem_reclaim_pending(void)
{
    unsigned i;
    for (i = 0; i < TIKU_MEM_MAX_TICKETS; i++) if (tickets[i].live) return 1;
    return 0;
}
int tiku_reclaim_reset_busy(void)
{
    unsigned i;
    if (job.active) return 1;
    for (i = 0; i < TIKU_MEM_MAX_TICKETS; i++)
        if (tickets[i].live && !terminal(&tickets[i])) return 1;
    return 0;
}
#include "tiku_reclaim_process.inl"

void tiku_mem_reclaim_poll(void)
{
    unsigned i; int first = -1;
    tiku_clock_time_t now;
    TIKU_MEM_KERNEL_ONLY_VOID();
    if (polling || TIKU_THIS() != NULL) return;
    polling = 1; now = tiku_clock_time();
    for (i = 0; i < TIKU_MEM_MAX_TICKETS; i++) {
        ticket_t *t = &tickets[i];
        if (!t->live) continue;
        if (!terminal(t) && t->process && (!t->process->is_running ||
            t->process->generation != t->process_generation)) cancel(t, TIKU_MEM_RECLAIM_CANCELLED);
        if (t->status.state == TIKU_MEM_TICKET_READY && due(now, t->status.deadline))
            cancel(t, TIKU_MEM_RECLAIM_EXPIRED);
        if (t->status.state == TIKU_MEM_TICKET_PENDING &&
            (t->status.phase == TIKU_MEM_RECLAIM_NONE || t->status.phase == TIKU_MEM_RECLAIM_PLAN) &&
            due(now, t->status.deadline)) cancel(t, TIKU_MEM_RECLAIM_EXPIRED);
        if (terminal(t) && due(now, t->status.deadline)) t->live = 0;
        else if (t->status.state == TIKU_MEM_TICKET_PENDING &&
                 (first < 0 || t->sequence < tickets[first].sequence)) first = (int)i;
    }
    if (job.active && !job.fault) {
        ticket_t *t = &tickets[job.ticket];
        switch (t->status.phase) {
        case TIKU_MEM_RECLAIM_PLAN: plan_step(); break;
        case TIKU_MEM_RECLAIM_COMMIT:
            if (!snapshot_unchanged() || !layout_valid() ||
                !tiku_reclaim_publish(job.old, job.layout, job.count, &job.layout[job.count])) {
                abort_job(TIKU_MEM_RECLAIM_CHANGED); break;
            }
            job.committed = 1;
            t->result = job.layout[job.count]; result_status(t);
            phase_begin(TIKU_MEM_RECLAIM_RESTORE);
            break;
        default: lifecycle_step(); break;
        }
    } else if (!job.active && first >= 0) admit((unsigned)first);
    polling = 0;
}

/** @brief Append a formatted line at @p *at; 0 when it does not fit. */
static int report_line(char *buf, size_t max, size_t *at, const char *format, ...)
{
    int n; va_list args;
    if (*at >= max) return 0;
    va_start(args, format);
    n = vsnprintf(buf + *at, max - *at, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= max - *at) return 0;
    *at += (size_t)n;
    return 1;
}

int tiku_mem_reclaim_read(const char *entry, char *buf, size_t max)
{
    static const char *const states[] = {"pending", "ready", "claimed", "cancelled", "failed", "fault"};
    static const char *const phases[] = {"none", "plan", "prepare", "commit", "restore", "abort"};
    static const char *const causes[] = {"ok", "capacity", "records", "no_layout", "search_limit",
        "refused", "timeout", "cancelled", "expired", "owner_fault", "detached", "disabled", "changed"};
    unsigned i; size_t at = 0;
    if (!entry || !buf || !max) return -1;
    buf[0] = 0;
    if (!strcmp(entry, "mode")) {
        if (!report_line(buf, max, &at, "%s\n", enabled ? "reactive" : "off")) return -1;
    } else if (!strcmp(entry, "job")) {
        if (!job.active) {
            if (!report_line(buf, max, &at, "idle\n")) return -1;
        } else {
            const ticket_t *t = &tickets[job.ticket];
            if (!report_line(buf, max, &at,
                "1:%lu ticket=%u:%lu state=%s phase=%s cause=%s owners=0x%x done=0x%x steps=%lu\n",
                (unsigned long)job.generation, (unsigned)(job.ticket + 1), (unsigned long)t->generation,
                states[t->status.state], phases[t->status.phase], causes[t->status.cause],
                (unsigned)job.owners, (unsigned)job.done, (unsigned long)job.steps)) return -1;
        }
    } else if (!strcmp(entry, "last")) {
        if (!last_job.generation) {
            if (!report_line(buf, max, &at, "none\n")) return -1;
        } else if (!report_line(buf, max, &at, "1:%lu state=%s cause=%s steps=%lu\n",
            (unsigned long)last_job.generation, states[last_job.state], causes[last_job.cause],
            (unsigned long)last_job.steps)) return -1;
    } else if (!strcmp(entry, "pending")) {
        for (i = 0; i < TIKU_MEM_MAX_TICKETS; i++) {
            const ticket_t *t = &tickets[i];
            if (!t->live) continue;
            if (!report_line(buf, max, &at, "%u:%lu %s %s %s size=%lu deadline=%lu\n",
                i + 1, (unsigned long)t->generation, states[t->status.state],
                phases[t->status.phase], causes[t->status.cause], (unsigned long)t->result.length,
                (unsigned long)t->status.deadline)) return -1;
        }
    } else if (!strcmp(entry, "owners")) {
        for (i = 0; i < TIKU_MEM_MAX_OWNERS; i++) {
            if (!owners[i].live) continue;
            if (!report_line(buf, max, &at, "%u:%lu %s %s prepare=%lu recovery=%lu type=%s restarts=%lu detail=%s\n", i + 1,
                (unsigned long)owners[i].generation, owners[i].name,
                tiku_mem_owner_available(owner_handle(i)) ? "available" : "gated",
                (unsigned long)owners[i].registration.prepare_ticks,
                (unsigned long)owners[i].registration.recovery_ticks,
                owners[i].registration.process ? "process" : "callback",
                (unsigned long)owners[i].process_restarts,
                owners[i].registration.describe ?
                    owners[i].registration.describe(owners[i].registration.context) : "none")) return -1;
        }
    } else if (!strcmp(entry, "stats")) {
        tiku_mem_reclaim_stats_t s;
        tiku_mem_reclaim_stats(&s);
        if (!report_line(buf, max, &at,
            "direct=%lu low=%lu high=%lu searched=%lu search_limit=%lu completed=%lu cancelled=%lu refused=%lu faults=%lu restored_slots=%lu replanned=%lu metadata=%lu\n",
            (unsigned long)s.direct, (unsigned long)s.layout_low, (unsigned long)s.layout_high,
            (unsigned long)s.searched, (unsigned long)s.search_limited, (unsigned long)s.completed,
            (unsigned long)s.cancelled, (unsigned long)s.refused, (unsigned long)s.faults,
            (unsigned long)s.restored_slots, (unsigned long)s.replanned,
            (unsigned long)s.metadata_bytes)) return -1;
    } else return -1;
    return (int)at;
}

tiku_mem_err_t tiku_mem_reclaim_write(const char *entry, const char *buf, size_t len)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!entry || !buf) return TIKU_MEM_ERR_INVALID;
    if (len && buf[len - 1] == '\n') len--;
    if (len && buf[len - 1] == '\r') len--;
    if (!strcmp(entry, "mode")) {
        if (len == 3 && !strncmp(buf, "off", 3)) return tiku_mem_reclaim_enable(0);
        if (len == 8 && !strncmp(buf, "reactive", 8)) return tiku_mem_reclaim_enable(1);
    } else if ((!strcmp(entry, "retry") || !strcmp(entry, "cancel")) &&
               len >= 3 && buf[0] == '1' && buf[1] == ':') {
        uint32_t generation = 0; size_t i;
        for (i = 2; i < len; i++) {
            unsigned digit = (unsigned)(buf[i] - '0');
            if (digit > 9 || generation > (UINT32_MAX - digit) / 10) return TIKU_MEM_ERR_INVALID;
            generation = generation * 10 + digit;
        }
        tiku_mem_job_t token = {generation, 1};
        if (!strcmp(entry, "retry")) return tiku_mem_reclaim_retry(token);
        /* Administrative VFS control, protected by CAP_SYS at the node; the
         * generation in the value must name the running job. */
        if (!job_equal(token)) return TIKU_MEM_ERR_INVALID;
        if (job.fault) return TIKU_MEM_ERR_BUSY;
        cancel(&tickets[job.ticket], TIKU_MEM_RECLAIM_CANCELLED);
        return TIKU_MEM_OK;
    }
    return TIKU_MEM_ERR_INVALID;
}

#endif
