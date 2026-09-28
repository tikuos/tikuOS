/* Owner-coordinated backing reconstruction. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_RECLAIM_H_
#define TIKU_RECLAIM_H_

#include "tiku_mem.h"

/* Opt in explicitly until a board's owner contracts have been qualified.
 * The ordinary reclaimable allocator does not depend on this feature. */
#ifndef TIKU_MEM_RECLAIM_ENABLE
#define TIKU_MEM_RECLAIM_ENABLE 0
#endif

#if TIKU_MEM_RECLAIM_ENABLE
#include "kernel/timers/tiku_clock.h"
#ifndef TIKU_MEM_MAX_OWNERS
#define TIKU_MEM_MAX_OWNERS 8
#endif
#ifndef TIKU_MEM_MAX_TICKETS
#define TIKU_MEM_MAX_TICKETS 4
#endif
#ifndef TIKU_MEM_RECLAIM_SEARCH_LIMIT
#define TIKU_MEM_RECLAIM_SEARCH_LIMIT 4096u
#endif
/** Plans retaken after the span moved under the planner, per job. */
#ifndef TIKU_MEM_RECLAIM_PLAN_ATTEMPTS
#define TIKU_MEM_RECLAIM_PLAN_ATTEMPTS 4u
#endif
#ifndef TIKU_MEM_RECLAIM_STEPS
#define TIKU_MEM_RECLAIM_STEPS 16u
#endif

typedef struct { uint32_t generation; uint16_t slot_plus_one; } tiku_mem_ticket_t;
typedef struct { uint32_t generation; uint16_t slot_plus_one; } tiku_mem_job_t;

typedef enum {
    TIKU_MEM_OWNER_PREPARE, TIKU_MEM_OWNER_ABORT, TIKU_MEM_OWNER_RESTORE
} tiku_mem_owner_phase_t;
typedef enum {
    TIKU_MEM_OWNER_DONE, TIKU_MEM_OWNER_WAIT,
    TIKU_MEM_OWNER_BUSY, TIKU_MEM_OWNER_FAULT
} tiku_mem_owner_result_t;
typedef tiku_mem_owner_result_t (*tiku_mem_owner_step_t)(
    void *context, tiku_mem_job_t job, tiku_mem_owner_phase_t phase);

/* Context and all descriptor/control objects must be stable, directly writable
 * kernel memory outside the backing selected for reconstruction. DONE in
 * PREPARE attests that all CPU, DMA and interrupt access has ended, and that
 * any needed snapshot is committed outside that backing. Callbacks must return;
 * a deadline cannot preempt a blocking callback or prove that hardware stopped. */
#define TIKU_MEM_OWNER_NVM 0x0100u
struct tiku_process;
typedef struct {
    const char *name;               /* copied, at most 23 bytes */
    void *context;
    size_t context_size;            /* checked against selected backing */
    tiku_mem_owner_step_t step;
    tiku_clock_time_t prepare_ticks, recovery_ticks;
    uint16_t flags;                 /* NVM requires an explicit restore contract */
    /* Optional scheduler process. It handles actions through the API below;
     * step is then the stopped-process cleanup callback after a failed init.
     * DONE from cleanup promises all partial CPU/DMA users have stopped.
     * Its INIT payload must be stable and have this declared size (0 for NULL). */
    struct tiku_process *process;
    size_t init_data_size;
    /* Optional read-only status token (no whitespace, static lifetime). Must
     * not dereference invalid backing while gated. Used by owners telemetry. */
    const char *(*describe)(void *context);
} tiku_mem_owner_registration_t;

typedef struct {
    tiku_mem_job_t job;
    tiku_mem_owner_phase_t phase;
    uint8_t rebuilding; /* ABORT may restart an exited process at its old addresses */
} tiku_mem_owner_action_t;

typedef enum {
    TIKU_MEM_REQUEST_ARENA = 1, TIKU_MEM_REQUEST_POOL = 2
} tiku_mem_request_kind_t;
typedef enum {
    TIKU_MEM_PLACE_WORKING, TIKU_MEM_PLACE_TIER, TIKU_MEM_PLACE_SPAN
} tiku_mem_placement_mode_t;
typedef struct {
    tiku_mem_request_kind_t kind;
    tiku_mem_placement_mode_t placement;
    tiku_mem_tier_t tier;
    uint8_t span_index;
    tiku_mem_arch_size_t size, count; /* arena: count=0; pool: size/count */
    tiku_mem_request_t options;
    tiku_mem_owner_t requester;     /* excluded from the stop set */
} tiku_mem_reclaim_request_t;

typedef enum {
    TIKU_MEM_TICKET_PENDING, TIKU_MEM_TICKET_READY, TIKU_MEM_TICKET_CLAIMED,
    TIKU_MEM_TICKET_CANCELLED, TIKU_MEM_TICKET_FAILED, TIKU_MEM_TICKET_FAULT
} tiku_mem_ticket_state_t;
typedef enum {
    TIKU_MEM_RECLAIM_NONE, TIKU_MEM_RECLAIM_PLAN, TIKU_MEM_RECLAIM_PREPARE,
    TIKU_MEM_RECLAIM_COMMIT, TIKU_MEM_RECLAIM_RESTORE, TIKU_MEM_RECLAIM_ABORT
} tiku_mem_reclaim_phase_t;
typedef enum {
    TIKU_MEM_RECLAIM_OK, TIKU_MEM_RECLAIM_CAPACITY, TIKU_MEM_RECLAIM_RECORDS,
    TIKU_MEM_RECLAIM_NO_LAYOUT, TIKU_MEM_RECLAIM_SEARCH_LIMITED,
    TIKU_MEM_RECLAIM_REFUSED, TIKU_MEM_RECLAIM_TIMEOUT,
    TIKU_MEM_RECLAIM_CANCELLED, TIKU_MEM_RECLAIM_EXPIRED,
    TIKU_MEM_RECLAIM_OWNER_FAULT, TIKU_MEM_RECLAIM_DETACHED,
    TIKU_MEM_RECLAIM_DISABLED, TIKU_MEM_RECLAIM_CHANGED
} tiku_mem_reclaim_cause_t;
typedef struct {
    tiku_mem_ticket_state_t state;
    tiku_mem_reclaim_phase_t phase;
    tiku_mem_reclaim_cause_t cause;
    tiku_mem_job_t job;
    tiku_clock_time_t deadline;
    tiku_mem_tier_t tier;
    uint8_t span_index;
    tiku_mem_arch_size_t offset, length;
} tiku_mem_reclaim_status_t;
typedef struct {
    uint32_t direct, layout_low, layout_high, searched, search_limited;
    uint32_t completed, cancelled, refused, faults;
    uint32_t restored_slots, replanned;
    size_t metadata_bytes;
} tiku_mem_reclaim_stats_t;

tiku_mem_err_t tiku_mem_owner_register(const tiku_mem_owner_registration_t *,
                                       tiku_mem_owner_t *);
tiku_mem_err_t tiku_mem_owner_unregister(tiku_mem_owner_t);
/* An ordinary command must check this before admitting new users or DMA. */
int tiku_mem_owner_available(tiku_mem_owner_t);
/* A process queries stable actions on POLL/INIT; events carry no action pointer.
 * PREPARE: save state, call ready(), then immediately TIKU_PROCESS_EXIT().
 * ABORT/RESTORE: reconstruct if rebuilding, then acknowledge DONE. WAIT yields;
 * BUSY refuses PREPARE. TIMER reaches the gated instance; other ordinary
 * events wait for job completion. */
tiku_mem_err_t tiku_mem_owner_process_action(tiku_mem_owner_t, tiku_mem_owner_action_t *);
tiku_mem_err_t tiku_mem_owner_process_ready(tiku_mem_owner_t, tiku_mem_job_t);
tiku_mem_err_t tiku_mem_owner_process_ack(tiku_mem_owner_t,
    const tiku_mem_owner_action_t *, tiku_mem_owner_result_t);
tiku_mem_err_t tiku_mem_reclaim_submit(const tiku_mem_reclaim_request_t *,
                                       tiku_mem_ticket_t *);
tiku_mem_err_t tiku_mem_reclaim_status(tiku_mem_ticket_t, tiku_mem_reclaim_status_t *);
tiku_mem_err_t tiku_mem_reclaim_claim_arena(tiku_mem_ticket_t, tiku_arena_t *);
tiku_mem_err_t tiku_mem_reclaim_claim_pool(tiku_mem_ticket_t, tiku_pool_t *);
tiku_mem_err_t tiku_mem_reclaim_cancel(tiku_mem_ticket_t);
tiku_mem_err_t tiku_mem_reclaim_forget(tiku_mem_ticket_t);
tiku_mem_err_t tiku_mem_reclaim_retry(tiku_mem_job_t);
tiku_mem_err_t tiku_mem_reclaim_enable(int enabled);
void tiku_mem_reclaim_stats(tiku_mem_reclaim_stats_t *);
/* One bounded unit of work. Scheduler calls it in kernel context; no extra
 * process registry slot. Reentrant calls are ignored. */
void tiku_mem_reclaim_poll(void);

#endif /* TIKU_MEM_RECLAIM_ENABLE */
#endif
