/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_reclaim.h - owner-coordinated reconstruction of tier backing.
 *
 * When no span has room for a request, the coordinator stops the registered
 * owners of restartable reservations, lays their reservations out again
 * around a gap for the request, and has the owners rebuild at the new places.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RECLAIM_H_
#define TIKU_RECLAIM_H_

#include "tiku_mem.h"

/**
 * @brief Builds the reconstruction coordinator; off by default.
 *
 * Enable it per board; the Makefile refuses it on MSP430.  The ordinary
 * reclaimable allocator does not depend on this feature.
 */
#ifndef TIKU_MEM_RECLAIM_ENABLE
#define TIKU_MEM_RECLAIM_ENABLE 0
#endif

#if TIKU_MEM_RECLAIM_ENABLE
#include "kernel/timers/tiku_clock.h"
/** @brief Owners that can be registered at once (1..16). */
#ifndef TIKU_MEM_MAX_OWNERS
#define TIKU_MEM_MAX_OWNERS 8
#endif
/** @brief Reclaim tickets that can be live at once (1..255). */
#ifndef TIKU_MEM_MAX_TICKETS
#define TIKU_MEM_MAX_TICKETS 4
#endif
/** @brief Layout search steps one job may take before SEARCH_LIMITED. */
#ifndef TIKU_MEM_RECLAIM_SEARCH_LIMIT
#define TIKU_MEM_RECLAIM_SEARCH_LIMIT 4096u
#endif
/** @brief Plans one job may retake after the span changed under it. */
#ifndef TIKU_MEM_RECLAIM_PLAN_ATTEMPTS
#define TIKU_MEM_RECLAIM_PLAN_ATTEMPTS 4u
#endif
/** @brief Layout search steps one tiku_mem_reclaim_poll() call may take. */
#ifndef TIKU_MEM_RECLAIM_STEPS
#define TIKU_MEM_RECLAIM_STEPS 16u
#endif

/** @brief Handle of a submitted request: ticket slot + 1 and its generation. */
typedef struct { uint32_t generation; uint16_t slot_plus_one; } tiku_mem_ticket_t;
/** @brief Handle of the running job; slot_plus_one is 1 while one runs. */
typedef struct { uint32_t generation; uint16_t slot_plus_one; } tiku_mem_job_t;

/**
 * @brief What a job asks of an owner.
 *
 * PREPARE: stop every use of the selected backing and save what must survive.
 * ABORT: the job was abandoned before commit; resume, rebuilding when told to.
 * RESTORE: rebuild the objects at their new places.
 */
typedef enum {
    TIKU_MEM_OWNER_PREPARE, TIKU_MEM_OWNER_ABORT, TIKU_MEM_OWNER_RESTORE
} tiku_mem_owner_phase_t;
/**
 * @brief An owner's answer to a phase.
 *
 * DONE: finished.  WAIT: not yet; asked again at a later poll.  BUSY refuses
 * PREPARE and FAULT fails it, either abandoning the job; in ABORT or RESTORE
 * both stop the job until tiku_mem_reclaim_retry().
 */
typedef enum {
    TIKU_MEM_OWNER_DONE, TIKU_MEM_OWNER_WAIT,
    TIKU_MEM_OWNER_BUSY, TIKU_MEM_OWNER_FAULT
} tiku_mem_owner_result_t;
/**
 * @brief Owner callback: carry out @p phase of @p job for @p context.
 *
 * tiku_mem_reclaim_poll() calls it again at later polls while it returns
 * TIKU_MEM_OWNER_WAIT, until the phase deadline passes.
 */
typedef tiku_mem_owner_result_t (*tiku_mem_owner_step_t)(
    void *context, tiku_mem_job_t job, tiku_mem_owner_phase_t phase);

/**
 * @brief Registration flag: the owner may hold NVM-tier reservations.
 *
 * Setting it is the owner's promise to restore their contents itself.
 */
#define TIKU_MEM_OWNER_NVM 0x0100u
struct tiku_process;
/** @brief An owner's contract with the coordinator. */
typedef struct {
    const char *name;               /**< copied, at most 23 bytes */
    void *context;                  /**< passed to step and describe */
    size_t context_size;            /**< bytes at context */
    tiku_mem_owner_step_t step;     /**< phase callback */
    tiku_clock_time_t prepare_ticks, recovery_ticks; /**< phase deadlines */
    uint16_t flags;                 /**< TIKU_MEM_OWNER_NVM or 0 */
    /** Optional process owner.  It takes its actions through
     *  tiku_mem_owner_process_action(), and step becomes the cleanup run
     *  after the gated process exits unexpectedly, before its restart. */
    struct tiku_process *process;
    size_t init_data_size;          /**< bytes of its INIT data; 0 for NULL */
    /** Optional status token for the owners report: no whitespace, static
     *  lifetime, and no access to backing while the owner is gated. */
    const char *(*describe)(void *context);
} tiku_mem_owner_registration_t;

/**
 * @brief The action a job asks of a process owner.
 *
 * With @c rebuilding set the old objects have ended: re-create them, at the
 * new places after commit or at the original ones in ABORT.
 */
typedef struct {
    tiku_mem_job_t job;
    tiku_mem_owner_phase_t phase;
    uint8_t rebuilding; /**< non-zero: re-create the objects first */
} tiku_mem_owner_action_t;

/**
 * @brief Kind of object a request is for.
 * @note The values equal TIKU_BACKING_ARENA and TIKU_BACKING_POOL
 *       (tiku_mem_internal.h), which the coordinator relies on.
 */
typedef enum {
    TIKU_MEM_REQUEST_ARENA = 1, TIKU_MEM_REQUEST_POOL = 2
} tiku_mem_request_kind_t;
/**
 * @brief Where a request may be placed.
 *
 * WORKING: as tiku_mem_arena_create() places, SRAM and HIFRAM, and PSRAM with
 * TIKU_MEM_ALLOW_EXTERNAL.  TIER: any span of @c tier.  SPAN: only span
 * @c span_index of @c tier.
 */
typedef enum {
    TIKU_MEM_PLACE_WORKING, TIKU_MEM_PLACE_TIER, TIKU_MEM_PLACE_SPAN
} tiku_mem_placement_mode_t;
/** @brief A request for backing, served by reconstruction when it must be. */
typedef struct {
    tiku_mem_request_kind_t kind;
    tiku_mem_placement_mode_t placement;
    tiku_mem_tier_t tier;           /**< for TIER and SPAN placement */
    uint8_t span_index;             /**< for SPAN placement */
    tiku_mem_arch_size_t size, count; /**< arena: count 0; pool: block size */
    tiku_mem_request_t options;     /**< alignment, flags, class and owner */
    tiku_mem_owner_t requester;     /**< requesting owner, never stopped */
} tiku_mem_reclaim_request_t;

/**
 * @brief Life of a ticket.
 *
 * PENDING: waiting, or its job is running.  READY: backing held; claim it
 * before the deadline.  CLAIMED, CANCELLED and FAILED are final.  FAULT: the
 * job faulted (see the cause) and waits for tiku_mem_reclaim_retry().
 */
typedef enum {
    TIKU_MEM_TICKET_PENDING, TIKU_MEM_TICKET_READY, TIKU_MEM_TICKET_CLAIMED,
    TIKU_MEM_TICKET_CANCELLED, TIKU_MEM_TICKET_FAILED, TIKU_MEM_TICKET_FAULT
} tiku_mem_ticket_state_t;
/**
 * @brief Phase of the job serving a ticket.
 *
 * NONE outside a job; PLAN searches for a layout; PREPARE stops the owners;
 * COMMIT publishes the layout; RESTORE has the owners rebuild; ABORT has them
 * resume after the job is abandoned.
 */
typedef enum {
    TIKU_MEM_RECLAIM_NONE, TIKU_MEM_RECLAIM_PLAN, TIKU_MEM_RECLAIM_PREPARE,
    TIKU_MEM_RECLAIM_COMMIT, TIKU_MEM_RECLAIM_RESTORE, TIKU_MEM_RECLAIM_ABORT
} tiku_mem_reclaim_phase_t;
/*
 * Why a ticket ended or faulted (TIKU_MEM_RECLAIM_ prefix omitted):
 *
 *   OK              served
 *   CAPACITY        no eligible span has that many free bytes, even split
 *   RECORDS         no free reservation record, or a slot generation is spent
 *   NO_LAYOUT       no span can be laid out again to fit, or a FIXED request
 *   SEARCH_LIMITED  the layout search reached TIKU_MEM_RECLAIM_SEARCH_LIMIT
 *   REFUSED         an owner answered BUSY to PREPARE
 *   TIMEOUT         an owner missed its phase deadline
 *   CANCELLED       cancelled, or the submitting process exited or restarted
 *   EXPIRED         not claimed in time, or still waiting at its deadline
 *   OWNER_FAULT     an owner failed, exited without consent, or reported
 *                   DONE without rebuilding
 *   DETACHED        a span the job or the result uses was detached
 *   DISABLED        the coordinator was turned off
 *   CHANGED         the reservation table changed under the plan
 */
/** @brief Why a ticket ended or faulted; see the table above. */
typedef enum {
    TIKU_MEM_RECLAIM_OK, TIKU_MEM_RECLAIM_CAPACITY, TIKU_MEM_RECLAIM_RECORDS,
    TIKU_MEM_RECLAIM_NO_LAYOUT, TIKU_MEM_RECLAIM_SEARCH_LIMITED,
    TIKU_MEM_RECLAIM_REFUSED, TIKU_MEM_RECLAIM_TIMEOUT,
    TIKU_MEM_RECLAIM_CANCELLED, TIKU_MEM_RECLAIM_EXPIRED,
    TIKU_MEM_RECLAIM_OWNER_FAULT, TIKU_MEM_RECLAIM_DETACHED,
    TIKU_MEM_RECLAIM_DISABLED, TIKU_MEM_RECLAIM_CHANGED
} tiku_mem_reclaim_cause_t;
/** @brief A ticket's status, as tiku_mem_reclaim_status() copies it. */
typedef struct {
    tiku_mem_ticket_state_t state;
    tiku_mem_reclaim_phase_t phase;
    tiku_mem_reclaim_cause_t cause;
    tiku_mem_job_t job;             /**< job serving the ticket, if any */
    tiku_clock_time_t deadline;     /**< claim, expiry or phase deadline */
    /** Where the result lies, once placed: tier, span, offset, length. */
    tiku_mem_tier_t tier;
    uint8_t span_index;
    tiku_mem_arch_size_t offset, length;
} tiku_mem_reclaim_status_t;
/** @brief Coordinator counters since boot. */
typedef struct {
    /** Requests held without a job; jobs laid out by the low or high fast
     *  layout or by search; jobs stopped at the search limit. */
    uint32_t direct, layout_low, layout_high, searched, search_limited;
    /** Jobs ended ready; tickets cancelled; jobs failed for any cause;
     *  job faults. */
    uint32_t completed, cancelled, refused, faults;
    /** Reservations rebound during restores; replans after a change. */
    uint32_t restored_slots, replanned;
    size_t metadata_bytes;          /**< static coordinator state */
} tiku_mem_reclaim_stats_t;

/**
 * @brief Register an owner whose reservations the coordinator may rebuild.
 *
 * A process owner must be running, have an ALWAYS or ON_FAILURE restart
 * policy, and not be registered already.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for a bad registration or a
 *         control object inside tier backing; TIKU_MEM_ERR_BUSY when the
 *         process is registered already or a poll is running;
 *         TIKU_MEM_ERR_FULL when no owner slot is free
 * @note The context, every arena or pool descriptor and control object, and
 *       a process owner's process and INIT data must be stable, directly
 *       writable kernel memory outside all tier backing.
 * @note DONE in PREPARE attests that all CPU, DMA and interrupt access to the
 *       backing has ended and any needed snapshot is committed outside it.
 *       DONE from a process owner's cleanup attests the same for the partial
 *       users of the failed instance.
 * @note Callbacks must return: a deadline cannot preempt a blocking callback
 *       or prove that hardware stopped.
 */
tiku_mem_err_t tiku_mem_owner_register(const tiku_mem_owner_registration_t *,
                                       tiku_mem_owner_t *);
/**
 * @brief Unregister an owner.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for a stale handle;
 *         TIKU_MEM_ERR_BUSY while a job selects it, it still owns a
 *         reservation, or a live ticket names it
 */
tiku_mem_err_t tiku_mem_owner_unregister(tiku_mem_owner_t);
/**
 * @brief Non-zero when the owner is registered and no job has it gated.
 * @note An ordinary command checks this before admitting new users or DMA.
 */
int tiku_mem_owner_available(tiku_mem_owner_t);
/**
 * @brief Fetch the action a job asks of the calling process owner.
 *
 * Events carry no action, so a gated process asks here on POLL or INIT; it
 * also receives TIMER.  Other events posted to it are refused until the job
 * releases it.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID when the caller is not this
 *         owner's current process; TIKU_MEM_ERR_NOT_FOUND when no action
 *         is due
 * @note PREPARE: save state, call tiku_mem_owner_process_ready(), then
 *       TIKU_PROCESS_EXIT() at once.  ABORT and RESTORE: rebuild when
 *       @c rebuilding is set, then acknowledge DONE.  WAIT yields; BUSY
 *       refuses PREPARE.
 */
tiku_mem_err_t tiku_mem_owner_process_action(tiku_mem_owner_t, tiku_mem_owner_action_t *);
/**
 * @brief Consent to PREPARE: the calling process exits next.
 *
 * Marks the coming exit as a reclaim exit.  Yielding instead of exiting
 * abandons the job with an owner fault.
 *
 * @return TIKU_MEM_OK, or TIKU_MEM_ERR_INVALID when no PREPARE is due for
 *         the caller, the reservation table changed, or it consented already
 */
tiku_mem_err_t tiku_mem_owner_process_ready(tiku_mem_owner_t, tiku_mem_job_t);
/**
 * @brief Report a process owner's result for its current action.
 *
 * WAIT changes nothing; DONE completes ABORT or RESTORE for this owner; BUSY
 * or FAULT abandons a PREPARE and faults a later phase.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID when the action is not the
 *         current one, for DONE to PREPARE, or for DONE when the objects are
 *         not rebuilt, which also faults the job
 */
tiku_mem_err_t tiku_mem_owner_process_ack(tiku_mem_owner_t,
    const tiku_mem_owner_action_t *, tiku_mem_owner_result_t);
/**
 * @brief Submit a request for backing, reconstructing other owners if needed.
 *
 * With no job running and no ticket waiting, a request that fits is READY at
 * once; otherwise tiku_mem_reclaim_poll() admits it and runs a job when it
 * must.  Claim the result with the claim call for its kind.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID or TIKU_MEM_ERR_NOMEM for a bad
 *         request; TIKU_MEM_ERR_BUSY while disabled or polling, when the
 *         requester or owner is gated, or the owner key is in use;
 *         TIKU_MEM_ERR_FULL when no ticket slot is free
 */
tiku_mem_err_t tiku_mem_reclaim_submit(const tiku_mem_reclaim_request_t *,
                                       tiku_mem_ticket_t *);
/**
 * @brief Copy a ticket's status.
 * @return TIKU_MEM_OK, or TIKU_MEM_ERR_INVALID for a stale ticket or NULL
 */
tiku_mem_err_t tiku_mem_reclaim_status(tiku_mem_ticket_t, tiku_mem_reclaim_status_t *);
/**
 * @brief Bind a READY arena ticket's backing to an arena descriptor.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for a stale or foreign ticket, a
 *         wrong kind, an expired claim, or an owned result whose descriptor
 *         lies in tier backing; TIKU_MEM_ERR_BUSY until the ticket is READY
 *         and its span unfenced, or while the descriptor is in use
 * @note Only the process instance that submitted the ticket, or kernel
 *       context when it was submitted there, may claim it.
 */
tiku_mem_err_t tiku_mem_reclaim_claim_arena(tiku_mem_ticket_t, tiku_arena_t *);
/** @brief tiku_mem_reclaim_claim_arena() for a pool ticket. */
tiku_mem_err_t tiku_mem_reclaim_claim_pool(tiku_mem_ticket_t, tiku_pool_t *);
/**
 * @brief Cancel a ticket: drop its result, or abandon its job before commit.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for a stale or foreign ticket;
 *         TIKU_MEM_ERR_BUSY once it is claimed
 * @note A faulted job is not abandoned; it waits for
 *       tiku_mem_reclaim_retry().
 */
tiku_mem_err_t tiku_mem_reclaim_cancel(tiku_mem_ticket_t);
/**
 * @brief Free a final ticket's slot now instead of at its deadline.
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for a stale or foreign ticket;
 *         TIKU_MEM_ERR_BUSY while it is not final
 */
tiku_mem_err_t tiku_mem_reclaim_forget(tiku_mem_ticket_t);
/**
 * @brief Resume a faulted job with fresh recovery deadlines.
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID unless the handle names the
 *         running, faulted job; TIKU_MEM_ERR_IO when a span it used was
 *         detached
 */
tiku_mem_err_t tiku_mem_reclaim_retry(tiku_mem_job_t);
/**
 * @brief Turn the coordinator on (1) or off (0); off cancels live tickets.
 * @return TIKU_MEM_OK, or TIKU_MEM_ERR_INVALID for any other value
 */
tiku_mem_err_t tiku_mem_reclaim_enable(int enabled);
/** @brief Copy the counters, with the size of the coordinator's state. */
void tiku_mem_reclaim_stats(tiku_mem_reclaim_stats_t *);
/**
 * @brief Do one bounded unit of coordinator work.
 *
 * Expires and frees tickets, then moves the running job on by one step or
 * admits the oldest waiting ticket.
 *
 * @note The scheduler calls it in kernel context, so it takes no process
 *       registry slot; calls from a process and reentrant calls return at
 *       once.
 */
void tiku_mem_reclaim_poll(void);

#endif /* TIKU_MEM_RECLAIM_ENABLE */
#endif
