/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_reclaim_internal.h - the coordinator's kernel-side interface.
 *
 * Hooks between the reconstruction coordinator and the tier allocator, the
 * process layer, the scheduler and the VFS; not application API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RECLAIM_INTERNAL_H_
#define TIKU_RECLAIM_INTERNAL_H_
#include "tiku_reclaim.h"

#if TIKU_MEM_RECLAIM_ENABLE
/** @brief A copy of one reservation record, as the coordinator plans with. */
typedef struct {
    const void *descriptor;         /**< bound arena or pool; NULL if held */
    tiku_mem_backing_t handle;
    tiku_mem_owner_t owner;         /**< registered owner, or zero */
    uint16_t owner_slot;            /**< the owner's key for it */
    tiku_mem_tier_t tier;
    /** Span within the tier; TIKU_BACKING_ARENA or _POOL; a tiku_mem_class_t;
     *  1 live, 2 held, 3 initializing. */
    uint8_t span_index, kind, allocation_class, state;
    /** Offset, length and alignment in the span; a pool's block size and
     *  count (0 for an arena). */
    tiku_mem_arch_size_t offset, length, alignment, block_size, block_count;
} tiku_reclaim_record_t;

/*
 * Only the coordinator can hold capacity or end nonempty owned lifetimes.
 * tiku_tier.c implements the record, hold, bind, credit, fence, publish,
 * restore, rearm and context-tag primitives below for it.
 */

/**
 * @brief Copy reservation record @p slot.
 * @return 1 when copied; 0 for a free slot, a credit, or a bad argument
 */
int tiku_reclaim_record(unsigned slot, tiku_reclaim_record_t *out);
/**
 * @brief Check a request and work out its record: length, alignment, a
 *        pool's block size and count, class and owner.
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID, or TIKU_MEM_ERR_NOMEM when the
 *         size overflows
 */
tiku_mem_err_t tiku_reclaim_normalize(const tiku_mem_reclaim_request_t *,
                                      tiku_reclaim_record_t *out);
/**
 * @brief Hold capacity for a normalized request where it fits as things are.
 * @return TIKU_MEM_OK with the record updated to the hold;
 *         TIKU_MEM_ERR_NOMEM when nothing fits; TIKU_MEM_ERR_FULL when no
 *         reservation record is free
 */
tiku_mem_err_t tiku_reclaim_hold_direct(tiku_reclaim_record_t *,
                                        const tiku_mem_reclaim_request_t *);
/**
 * @brief Build an arena or pool over a held reservation and make it live.
 *
 * @param restoring  Non-zero for a rebuild, which is not counted as a new
 *                   allocation
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID unless the handle names a hold;
 *         TIKU_MEM_ERR_BUSY when the descriptor already has a reservation;
 *         or the pool creation error
 */
tiku_mem_err_t tiku_reclaim_bind(tiku_mem_backing_t, void *descriptor, uint8_t id, int restoring);
/** @brief Release a hold or a credit; any other handle is ignored. */
void tiku_reclaim_drop(tiku_mem_backing_t);
/**
 * @brief Take a free reservation record as a credit: metadata, no span bytes.
 * @return 1 with the handle set, or 0 when no record is free
 */
int tiku_reclaim_credit(tiku_mem_backing_t *);
/**
 * @brief Set or clear a span's fence.
 *
 * A fenced span takes no new reservation and releases none, so the plan for
 * it holds still.
 *
 * @return 1, or 0 for a span that does not exist or is not initialized
 */
int tiku_reclaim_fence(tiku_mem_tier_t, uint8_t, int enabled);
/** @brief Non-zero when the span is fenced. */
int tiku_reclaim_fenced(tiku_mem_tier_t, uint8_t);
/** @brief Non-zero when any span is fenced. */
int tiku_reclaim_any_fence(void);
/** @brief Non-zero while a job runs or a ticket is not final. */
int tiku_reclaim_reset_busy(void);
/**
 * @brief Non-zero while any ticket is live; the scheduler then sleeps no
 *        longer than one tick.
 */
int tiku_mem_reclaim_pending(void);
struct tiku_process;
/**
 * @brief Exit hook: cancel the process's tickets and settle a gated exit.
 *
 * A consented exit completes the owner's PREPARE.  Any other exit of a gated
 * process faults the job, abandoning it first when it has not committed.
 *
 * @return Non-zero when the coordinator takes over the exit, so supervision
 *         does not restart the process
 */
int tiku_mem_reclaim_process_exit(struct tiku_process *);
/** @brief Non-zero when the process consented to the running PREPARE. */
int tiku_mem_reclaim_process_exit_valid(const struct tiku_process *);
/** @brief Zero while the process is gated, unless the job is starting it. */
int tiku_mem_reclaim_process_start_allowed(const struct tiku_process *);
/** @brief Start hook: bind the process's new instance to its owner. */
void tiku_mem_reclaim_process_started(struct tiku_process *);
/**
 * @brief Non-zero when @p event may reach the process.
 *
 * A gated process gets only POLL, INIT and TIMER, and only while it has an
 * action due; other processes get everything.
 */
int tiku_mem_reclaim_process_dispatch(const struct tiku_process *, unsigned event);
/** @brief Yield hook: a process that consented and yielded faults the job. */
void tiku_mem_reclaim_process_yielded(struct tiku_process *);
/**
 * @brief Charge a failed restore to the ordinary crash-storm allowance.
 * @return 1 when charged, 0 when the policy is TIKU_RESTART_NEVER or the
 *         storm limit disarmed supervision (tiku_process.c)
 */
int tiku_process_restart_charge(struct tiku_process *);
/**
 * @brief Non-zero when no live event but POLL is queued for the process,
 *        broadcasts included (tiku_process.c).
 */
int tiku_process_reclaim_quiet(const struct tiku_process *);
/**
 * @brief Format one status entry: mode, job, last, pending, owners or stats.
 * @return Bytes written, or -1 for an unknown entry or a short buffer
 */
int tiku_mem_reclaim_read(const char *entry, char *buf, size_t size);
/**
 * @brief Apply a control write: mode off|reactive, retry 1:<generation> or
 *        cancel 1:<generation>.
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for a bad entry, value or job;
 *         TIKU_MEM_ERR_BUSY to cancel a faulted job; or the retry error
 */
tiku_mem_err_t tiku_mem_reclaim_write(const char *entry, const char *buf, size_t size);
/**
 * @brief Commit a layout: end the old lifetimes and turn the credits into
 *        holds at their new places, the request's included.
 * @return 1 when published, 0 when a check failed and nothing changed
 * @note Every check precedes the first change; the switch runs with
 *       interrupts masked and makes no callback or backing write.
 */
int tiku_reclaim_publish(const tiku_reclaim_record_t *old,
                         tiku_reclaim_record_t *replacement, unsigned count,
                         tiku_reclaim_record_t *request);
/**
 * @brief For one owner, end its old lifetimes and hold its credits at the
 *        original places, for its restarted process to bind.
 * @return 1, or 0 when a check failed and nothing changed
 */
int tiku_reclaim_restore_original(const tiku_reclaim_record_t *,
    tiku_reclaim_record_t *, unsigned count, tiku_mem_owner_t);
/**
 * @brief Turn an owner's live records in a layout back into holds.
 *
 * Used after a failed initializer's cleanup has stopped every access.
 *
 * @return 1, or 0 when a check failed and nothing changed
 */
int tiku_reclaim_rearm(tiku_reclaim_record_t *, unsigned count, tiku_mem_owner_t);
/**
 * @brief Non-zero when the owner is a process owner and the given control
 *        object lies outside tier backing.
 */
int tiku_reclaim_process_owner_valid(tiku_mem_owner_t, const void *, size_t);
/**
 * @brief Hand a process memory context's arenas to a process owner.
 *
 * The SRAM, NVM and HIFRAM arenas become restartable under the given key
 * base, plus 1 and plus 2.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_BUSY while the owner is gated, a span is
 *         fenced or a key is in use; TIKU_MEM_ERR_INVALID when an arena is
 *         not a tracked transient one or cannot be owned, or none is active
 */
tiku_mem_err_t tiku_reclaim_context_tag(tiku_proc_mem_t *, tiku_mem_owner_t, uint16_t);

/* Callouts from tier operations. */
/**
 * @brief Check a request's owner fields and resolve its allocation class.
 * @return 1 with @p cls set, or 0 when the owner, key or class is refused
 */
int tiku_reclaim_owner_options(const tiku_mem_request_t *, uint8_t *cls);
/**
 * @brief Owner checks for a tier create.
 *
 * For an owner that is not gated, a key in use is refused.  While the owner
 * rebuilds in RESTORE, or in an ABORT that ended its objects, the create
 * binds the matching hold instead and sets @p claimed.
 *
 * @return TIKU_MEM_OK; TIKU_MEM_ERR_INVALID for a bad owner or descriptor,
 *         NVM without TIKU_MEM_OWNER_NVM, or no matching hold;
 *         TIKU_MEM_ERR_BUSY while the owner is gated and not rebuilding, or
 *         the key is in use; or the bind error
 */
tiku_mem_err_t tiku_reclaim_owner_create(const tiku_mem_request_t *,
    tiku_mem_tier_t, int span, uint8_t kind, tiku_mem_arch_size_t length,
    tiku_mem_arch_size_t alignment, tiku_mem_arch_size_t stride,
    tiku_mem_arch_size_t count, void *descriptor, uint8_t id, int *claimed);
/**
 * @brief Non-zero when the owner's objects may change now: no owner, the
 *        owner is not gated, or it acts for the running job.
 */
int tiku_reclaim_owner_access(tiku_mem_owner_t);
/** @brief A span was detached: fault the job using it, fail READY tickets. */
void tiku_reclaim_detached(tiku_mem_tier_t, uint8_t);

#endif
#endif
