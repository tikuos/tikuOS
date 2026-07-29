/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_process.c - Process management implementation
 *
 * Implements the event-driven cooperative scheduler. Processes are
 * linked in a singly-linked list and communicate through an event
 * queue that is safe to post to from interrupt context.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_process.h"
#include <kernel/memory/tiku_mem.h> /* measured accounting (attached arena) */
#include <hal/tiku_compiler.h>
#include <hal/tiku_cpu.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_timer.h>  /* tiku_timer_owner_armed (SLEEPING) */
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
#include <kernel/threads/tiku_thread.h> /* kernel_wake on every post */
#endif
#include <kernel/cpu/tiku_hang.h>    /* one-shot quarantine of a hung process */
#include <stddef.h>
#include <stdint.h>   /* uintptr_t for the typed-event payload accessors */
#include <string.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE TYPES                                                             */
/*---------------------------------------------------------------------------*/

/*
 * One slot in the event queue ring: a single posted event in flight between the
 * poster, which may be an ISR, and the dispatcher.  The target is a process or
 * BROADCAST.  The wider data pointer leads, keeping the struct packed on 16-
 * and 32-bit alike.
 */
struct event_item {
    tiku_event_data_t data;     /**< Opaque payload passed to the thread */
    struct tiku_process *p;     /**< Target process, or BROADCAST (NULL) */
    uint8_t generation;         /**< Target generation when event was posted */
    tiku_event_t ev;            /**< Event identifier (TIKU_EVENT_*) */
};

/*---------------------------------------------------------------------------*/
/* PRIVATE VARIABLES                                                         */
/*---------------------------------------------------------------------------*/

/*
 * The event queue ring: a fixed-size circular buffer written by post and poll,
 * possibly from an ISR, and drained one entry per run.  The slots are not
 * volatile because every access sits in an atomic section; only the head and
 * length indices are.
 */
static struct event_item queue[TIKU_QUEUE_SIZE];

/*
 * Index of the oldest pending event.  Advances on each dequeue.  volatile
 * because an ISR posting reads it to compute the tail, and only ever mutated
 * inside an atomic section.
 */
static volatile uint8_t q_head = 0;

/*
 * Number of events in the ring.  The tail is derived as head + length, so no
 * separate tail index is kept; volatile and atomic-guarded for the same
 * ISR-versus-process reason as the head.
 */
static volatile uint8_t q_len = 0;

/*
 * Lifetime count of events dropped because the queue was full at post time.
 * Bumped inside the same atomic section as the failed enqueue, so it is exact.
 * Surfaced at /proc/queue/dropped -- observability for an otherwise silent failure.
 */
static volatile uint16_t q_dropped = 0;

/*
 * Is this a kernel event rather than an application one?  System events occupy
 * the low control range and the high kernel range, applications the span
 * between.  The distinction feeds the reserve admission check in post().
 */
static inline uint8_t event_is_system(tiku_event_t ev)
{
    return (ev < TIKU_EVENT_USER) || (ev >= TIKU_EVENT_TIMER);
}

/*
 * Event generation for a target process.  A broadcast names no single lifetime
 * so it carries 0; a unicast carries the target's generation, which is what
 * lets a stale event for a restarted process be dropped.
 */
static inline uint8_t event_generation(const struct tiku_process *p)
{
    return (p != TIKU_PROCESS_BROADCAST) ? p->generation : 0u;
}

/*
 * Drop queued unicast events for a process; the caller already holds the queue
 * atomic section.  Broadcasts are kept, because they are not private wakeups
 * and go to whichever processes are running when they reach the head.
 */
static void queue_purge_process_locked(const struct tiku_process *p)
{
    uint8_t i;
    uint8_t new_len = 0;
    uint8_t old_len = q_len;

    for (i = 0; i < old_len; i++) {
        uint8_t old_idx = (uint8_t)((q_head + i) % TIKU_QUEUE_SIZE);

        if (queue[old_idx].p != p) {
            uint8_t new_idx = (uint8_t)((q_head + new_len) % TIKU_QUEUE_SIZE);
            if (new_idx != old_idx) {
                queue[new_idx] = queue[old_idx];
            }
            new_len++;
        }
    }

    q_len = new_len;
}

/**
 * @brief Remove the queued event at ring position @p pos, compacting the queue.
 *
 * Runs inside the caller's atomic section.  The removed slot is copied out when
 * @p out is non-NULL, then the tail shifts down one and the length drops.
 *
 * @param pos  Offset from q_head of the entry to remove (0 = oldest pending).
 * @param out  Optional destination for the removed event (may be NULL).
 */
static void queue_remove_locked(uint8_t pos, struct event_item *out)
{
    uint8_t i;

    if (out != NULL) {
        *out = queue[(q_head + pos) % TIKU_QUEUE_SIZE];
    }

    for (i = pos; (uint8_t)(i + 1u) < q_len; i++) {
        uint8_t dst = (uint8_t)((q_head + i) % TIKU_QUEUE_SIZE);
        uint8_t src = (uint8_t)((q_head + i + 1u) % TIKU_QUEUE_SIZE);
        queue[dst] = queue[src];
    }
    q_len--;
}

static inline uint8_t event_is_stale(const struct tiku_process *p,
                                     uint8_t generation)
{
    return p != TIKU_PROCESS_BROADCAST && generation != p->generation;
}

static uint8_t queue_has_dispatchable_except_locked(
    const struct tiku_process *skip)
{
    uint8_t i;

    if (skip == NULL) {
        return q_len != 0u;
    }

    for (i = 0; i < q_len; i++) {
        const struct event_item *item =
            &queue[(q_head + i) % TIKU_QUEUE_SIZE];

        if (event_is_stale(item->p, item->generation)) {
            return 1u;              /* stale work can be discarded */
        }
        if (item->p != skip) {
            return 1u;              /* normal dispatch, incl. broadcast */
        }
        if (item->ev == TIKU_EVENT_POLL) {
            return 1u;              /* skip is already awake; coalesce it */
        }
    }

    return 0u;
}

/*
 * Process registry, indexed by pid, NULL where free.  Stopping a process does
 * NOT vacate its slot -- the pid stays reserved so /proc and the shell can
 * still inspect and resume it.  Touched only from process context.
 */
static struct tiku_process *registry[TIKU_PROCESS_MAX];

/*---------------------------------------------------------------------------*/
/* PUBLIC VARIABLES                                                          */
/*---------------------------------------------------------------------------*/

/*
 * Head of the list of started processes, walked to fan out a broadcast.
 * Insertion and removal happen inside atomic sections, because a broadcast post
 * from an ISR could otherwise see a half-linked node.
 */
struct tiku_process *tiku_process_list_head = NULL;

/*
 * The process currently executing, set for the duration of one thread call.
 * TIKU_THIS() and TIKU_LOCAL() read it, so a thread body reaches its own
 * control block without being passed a self pointer.  Meaningful only mid-call.
 */
struct tiku_process *tiku_current_process = NULL;

/**
 * @brief Default (empty) autostart list
 *
 * Weak symbol so that user code can override it via
 * TIKU_AUTOSTART_PROCESSES(). If no override is provided,
 * the scheduler starts with no autostart processes.
 */
TIKU_WEAK struct tiku_process * const tiku_autostart_processes[] = {NULL};

/*---------------------------------------------------------------------------*/
/* PRIVATE FUNCTION PROTOTYPES                                               */
/*---------------------------------------------------------------------------*/

static void call_process(struct tiku_process *p, tiku_event_t ev,
                         tiku_event_data_t data);

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the process scheduler
 *
 * Resets the process list and event queue to their initial state.
 * Must be called before interrupts are enabled.
 */
void tiku_process_init(void)
{
    uint8_t i;
    struct tiku_process *p, *next;

    /* Detach every process previously on the list and clear its
     * is_running flag.  Without this, a re-register after init()
     * would see the stale is_running=1 and skip tiku_process_start(),
     * leaving the process stranded in the registry but unlinked from
     * tiku_process_list_head — its protothread state, wake_count,
     * start_time, and state field would all carry over from the
     * previous boot/test, breaking observability and the scheduler. */
    for (p = tiku_process_list_head; p != NULL; p = next) {
        next = p->next;
        p->is_running = 0;
        p->next = NULL;
    }

    tiku_process_list_head = NULL;
    tiku_current_process = NULL;
    q_head = 0;
    q_len = 0;
    q_dropped = 0;

    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        registry[i] = NULL;
    }

    PROCESS_PRINTF("Init complete\n");
}

/**
 * @brief Start a process.
 *
 * Idempotent.  Re-inits the protothread and links it at the list head inside an
 * atomic section, so an ISR broadcast never walks a half-linked node.  A full
 * queue falls back to a synchronous call, so a start always yields one INIT.
 *
 * @param p    Process to start
 * @param data Data passed with the INIT event
 */
void tiku_process_start(struct tiku_process *p, tiku_event_data_t data)
{
    if (p->is_running) {
        return;
    }

    /* Protect list modification — an ISR could post a broadcast event
     * midway through linking a new node into the list. */
    tiku_atomic_enter();

    PT_INIT(&p->pt);
    p->generation++;
    if (p->generation == 0u) {
        p->generation = 1u;
    }

    p->next = tiku_process_list_head;
    tiku_process_list_head = p;
    p->is_running = 1;
    p->state = TIKU_PROCESS_STATE_READY;
    p->start_time = tiku_clock_time();
    p->wake_count = 0;
    p->exit_reason = (uint8_t)TIKU_EXIT_NONE;   /* fresh instance */
    p->init_data = data;                        /* replayed if supervised */

    tiku_atomic_exit();

    PROCESS_PRINTF("Started: %s\n", p->name);

    /* Ensure INIT is delivered even if the queue is full. */
    if (!tiku_process_post(p, TIKU_EVENT_INIT, data)) {
        call_process(p, TIKU_EVENT_INIT, data);
    }
}

/**
 * @brief Exit a process.
 *
 * Clears the running flag and unlinks from the list inside an atomic section,
 * then broadcasts EXITED so subsystems holding per-process resources can
 * release them.  The registry slot and pid survive, so it can still be resumed.
 *
 * @param p Process to exit
 */
/* Supervision (definitions below tiku_process_exit, which calls this). */
static void supervisor_on_exit(struct tiku_process *p);

/** Restart-storm cap: at most this many restarts within the window before
 *  the supervisor gives up (falls back to NEVER) rather than looping. */
#ifndef TIKU_SUPERVISOR_MAX_BURST
#define TIKU_SUPERVISOR_MAX_BURST   5u
#endif
/** Window (ticks) over which restarts are counted toward the burst cap.
 *  Restarts spaced further apart than this reset the count -- only a genuine
 *  storm trips it. */
#ifndef TIKU_SUPERVISOR_WINDOW_TICKS
#define TIKU_SUPERVISOR_WINDOW_TICKS  (5u * TIKU_CLOCK_SECOND)
#endif

void tiku_process_exit(struct tiku_process *p)
{
    struct tiku_process *q;

    if (!p->is_running) {
        return;
    }

    PROCESS_PRINTF("Exited: %s\n", p->name);

    /* Protect list modification — same rationale as tiku_process_start */
    tiku_atomic_enter();

    p->is_running = 0;
    p->state = TIKU_PROCESS_STATE_STOPPED;

    if (tiku_process_list_head == p) {
        tiku_process_list_head = p->next;
    } else {
        for (q = tiku_process_list_head; q != NULL; q = q->next) {
            if (q->next == p) {
                q->next = p->next;
                break;
            }
        }
    }
    queue_purge_process_locked(p);

    tiku_atomic_exit();

    /* Notify other processes (e.g. timer process) so they can
     * clean up resources belonging to the exited process.  The
     * data pointer carries the exited process's identity. */
    tiku_process_post_proc(TIKU_PROCESS_BROADCAST, TIKU_EVENT_EXITED, p);

    /* Supervision: per the process's restart policy, bring it straight back
     * as a fresh instance (same pid) instead of leaving recovery to a human
     * or a whole-board reboot.  NEVER (the default) makes this a no-op, so
     * unsupervised processes are unaffected. */
    supervisor_on_exit(p);
}

/*---------------------------------------------------------------------------*/
/* SUPERVISION                                                                */
/*---------------------------------------------------------------------------*/

/*
 * Restart @p p per its policy.  ALWAYS restarts on any exit; ON_FAILURE only
 * when it ended FAILED.  A restart is a fresh tiku_process_start() -- the pid
 * + registry slot survived tiku_process_exit(), so the new instance keeps the
 * same identity and stays observable, and only this process is touched (the
 * rest of the system keeps running).  A storm -- too many restarts inside the
 * window -- trips the burst cap: the policy is forced to NEVER so the run loop
 * can't spin on a process that fails immediately on every restart.
 */
static void supervisor_on_exit(struct tiku_process *p)
{
    tiku_clock_time_t now;

    if (p->restart == (uint8_t)TIKU_RESTART_NEVER) {
        return;
    }
    if (p->restart == (uint8_t)TIKU_RESTART_ON_FAILURE &&
        p->exit_reason != (uint8_t)TIKU_EXIT_FAILED) {
        return;                     /* clean exit + ON_FAILURE -> leave stopped */
    }

    now = tiku_clock_time();
    /* A restart spaced further than the window from the last one starts a
     * fresh burst -- only a genuine storm accumulates toward the cap. */
    if ((tiku_clock_time_t)(now - p->restart_at) >
        (tiku_clock_time_t)TIKU_SUPERVISOR_WINDOW_TICKS) {
        p->restart_burst = 0;
    }
    if (p->restart_burst >= (uint8_t)TIKU_SUPERVISOR_MAX_BURST) {
        /* Give up rather than loop: leave STOPPED and disarm supervision
         * until something re-arms it. */
        p->restart = (uint8_t)TIKU_RESTART_NEVER;
        return;
    }

    p->restart_burst++;
    if (p->restart_total != 0xFFFFu) {
        p->restart_total++;
    }
    p->restart_at = now;

    tiku_process_start(p, p->init_data);        /* fresh instance, same pid */
}

void tiku_process_set_restart(struct tiku_process *p,
                              tiku_restart_policy_t policy)
{
    if (p != NULL) {
        p->restart = (uint8_t)policy;
    }
}

void tiku_process_fail(struct tiku_process *p)
{
    if (p != NULL) {
        p->exit_reason = (uint8_t)TIKU_EXIT_FAILED;
    }
}

tiku_restart_policy_t tiku_process_get_restart(const struct tiku_process *p)
{
    return (p != NULL) ? (tiku_restart_policy_t)p->restart : TIKU_RESTART_NEVER;
}

tiku_exit_reason_t tiku_process_exit_reason(const struct tiku_process *p)
{
    return (p != NULL) ? (tiku_exit_reason_t)p->exit_reason : TIKU_EXIT_NONE;
}

uint16_t tiku_process_restarts(const struct tiku_process *p)
{
    return (p != NULL) ? p->restart_total : 0u;
}

/**
 * @brief Post an event to a process.
 *
 * Appends to the ring tail, returning 0 when full so the caller decides how to
 * recover.  Safe from interrupt context, since the read-modify-write sits in an
 * atomic section.  Delivery happens later, when run() drains from process context.
 *
 * @param p    Target process (or TIKU_PROCESS_BROADCAST)
 * @param ev   Event identifier
 * @param data Event data
 * @return 1 if event posted, 0 if queue full
 */
uint8_t tiku_process_post(struct tiku_process *p, tiku_event_t ev,
                          tiku_event_data_t data)
{
    uint8_t ret = 0;
    uint8_t limit;

    tiku_atomic_enter();

    /* System events may use every slot; user-range events stop
     * TIKU_QUEUE_RESERVE short so an application flood can never
     * drop a kernel event (see TIKU_QUEUE_RESERVE). */
    limit = event_is_system(ev) ? TIKU_QUEUE_SIZE
                                : (TIKU_QUEUE_SIZE - TIKU_QUEUE_RESERVE);

    if (q_len < limit) {
        uint8_t idx = (q_head + q_len) % TIKU_QUEUE_SIZE;
        queue[idx].ev = ev;
        queue[idx].data = data;
        queue[idx].p = p;
        queue[idx].generation = event_generation(p);
        q_len++;
        ret = 1;
    } else {
        q_dropped++;
    }

    tiku_atomic_exit();

#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
    /* New work exists: the kernel thread (absolute priority) preempts
     * any running worker at the next unmasked instant. */
    if (ret) {
        tiku_thread_kernel_wake();
    }
#endif

    return ret;
}

/*---------------------------------------------------------------------------*/
/* TYPED EVENT PAYLOADS                                                       */
/*---------------------------------------------------------------------------*/

tiku_event_payload_kind_t tiku_event_payload_kind(tiku_event_t ev)
{
    switch (ev) {
    case TIKU_EVENT_EXITED: return TIKU_EVENT_PAYLOAD_PROC;
    case TIKU_EVENT_VFS:    return TIKU_EVENT_PAYLOAD_NODE;
    case TIKU_EVENT_TIMER:  return TIKU_EVENT_PAYLOAD_TIMER;
    case TIKU_EVENT_GPIO:   return TIKU_EVENT_PAYLOAD_U32;
    case TIKU_EVENT_INIT:   return TIKU_EVENT_PAYLOAD_PTR;
    default:
        /* USER-range events carry an app pointer; the system control events
         * (EXIT/CONTINUE/POLL/FORCE_EXIT) carry nothing. */
        return (ev >= TIKU_EVENT_USER && ev < TIKU_EVENT_TIMER)
                   ? TIKU_EVENT_PAYLOAD_PTR
                   : TIKU_EVENT_PAYLOAD_NONE;
    }
}

struct tiku_process *tiku_event_proc(tiku_event_t ev, tiku_event_data_t data)
{
    return (tiku_event_payload_kind(ev) == TIKU_EVENT_PAYLOAD_PROC)
               ? (struct tiku_process *)data
               : NULL;
}

const struct tiku_vfs_node *tiku_event_node(tiku_event_t ev,
                                            tiku_event_data_t data)
{
    return (tiku_event_payload_kind(ev) == TIKU_EVENT_PAYLOAD_NODE)
               ? (const struct tiku_vfs_node *)data
               : NULL;
}

struct tiku_timer *tiku_event_timer(tiku_event_t ev, tiku_event_data_t data)
{
    return (tiku_event_payload_kind(ev) == TIKU_EVENT_PAYLOAD_TIMER)
               ? (struct tiku_timer *)data
               : NULL;
}

/**
 * @brief Extract the packed 32-bit integer payload carried by an event.
 *
 * @param ev    Event identifier.
 * @param data  Raw event payload word.
 * @return The u32 value, or 0 if @p ev does not carry a U32 payload.
 */
uint32_t tiku_event_u32(tiku_event_t ev, tiku_event_data_t data)
{
    return (tiku_event_payload_kind(ev) == TIKU_EVENT_PAYLOAD_U32)
               ? (uint32_t)(uintptr_t)data
               : 0u;
}

/**
 * @brief Extract the opaque application pointer carried by an event.
 *
 * @param ev    Event identifier.
 * @param data  Raw event payload word.
 * @return The pointer, or NULL if @p ev does not carry a PTR payload.
 */
void *tiku_event_ptr(tiku_event_t ev, tiku_event_data_t data)
{
    return (tiku_event_payload_kind(ev) == TIKU_EVENT_PAYLOAD_PTR) ? data : NULL;
}

uint8_t tiku_process_post_proc(struct tiku_process *dest, tiku_event_t ev,
                               struct tiku_process *arg)
{
    return tiku_process_post(dest, ev, (tiku_event_data_t)arg);
}

uint8_t tiku_process_post_node(struct tiku_process *dest, tiku_event_t ev,
                               const struct tiku_vfs_node *node)
{
    /* Payload is read-only to consumers (tiku_event_node returns const); the
     * wire is a bare void*, so strip const through uintptr_t. */
    return tiku_process_post(dest, ev, (tiku_event_data_t)(uintptr_t)node);
}

/**
 * @brief Run the process scheduler.
 *
 * Dequeues and dispatches exactly one event.  The dequeue is atomic but the
 * dispatch deliberately is not, since holding interrupts off across a thread
 * call would inflate latency.  A broadcast caches each next pointer first.
 *
 * @return 1 if an event was processed, 0 if idle
 */
uint8_t tiku_process_run(void)
{
    tiku_event_t ev;
    tiku_event_data_t data;
    struct tiku_process *receiver;
    uint8_t generation;

    tiku_atomic_enter();

    if (q_len == 0) {
        tiku_atomic_exit();
        return 0;
    }

    ev = queue[q_head].ev;
    data = queue[q_head].data;
    receiver = queue[q_head].p;
    generation = queue[q_head].generation;
    q_head = (q_head + 1) % TIKU_QUEUE_SIZE;
    q_len--;

    tiku_atomic_exit();

    /* Dispatch outside atomic section to avoid long interrupt latency */
    if (event_is_stale(receiver, generation)) {
        return 1;
    }
    if (receiver == TIKU_PROCESS_BROADCAST) {
        struct tiku_process *p, *next;
        for (p = tiku_process_list_head; p != NULL; p = next) {
            next = p->next;
            call_process(p, ev, data);
        }
    } else {
        call_process(receiver, ev, data);
    }

    return 1;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Dispatch one queued event, but never re-enter @p skip.
 *
 * For a long synchronous operation running INSIDE @p skip's own dispatch: it
 * may pump the other processes so timers and rules keep firing, but must not
 * recursively re-enter a protothread whose saved state points at its last yield.
 *
 * @param skip Process not to dispatch (typically TIKU_THIS()); NULL == plain run
 * @return 1 if an event was dispatched/discarded, 0 if no eligible work exists
 */
uint8_t tiku_process_run_except(const struct tiku_process *skip)
{
    struct event_item item;
    uint8_t i;
    uint8_t found = 0u;

    tiku_atomic_enter();

    if (skip == NULL) {
        tiku_atomic_exit();
        return tiku_process_run();
    }

    for (i = 0; i < q_len; i++) {
        struct event_item *candidate =
            &queue[(q_head + i) % TIKU_QUEUE_SIZE];

        if (event_is_stale(candidate->p, candidate->generation) ||
            candidate->p != skip ||
            candidate->ev == TIKU_EVENT_POLL) {
            queue_remove_locked(i, &item);
            found = 1u;
            break;
        }
    }

    tiku_atomic_exit();

    if (!found) {
        return 0;
    }
    if (event_is_stale(item.p, item.generation)) {
        return 1;                         /* stale unicast discarded */
    }
    if (item.p == skip) {
        return 1;                         /* only POLL reaches this path */
    }
    if (item.p == TIKU_PROCESS_BROADCAST) {
        struct tiku_process *p, *next;
        for (p = tiku_process_list_head; p != NULL; p = next) {
            next = p->next;
            if (p != skip) {
                call_process(p, item.ev, item.data);
            }
        }
    } else {
        call_process(item.p, item.ev, item.data);
    }

    return 1;
}

/*---------------------------------------------------------------------------*/

uint8_t tiku_process_queue_dispatchable_except(const struct tiku_process *skip)
{
    uint8_t ret;

    tiku_atomic_enter();
    ret = queue_has_dispatchable_except_locked(skip);
    tiku_atomic_exit();

    return ret;
}

/**
 * @brief Request a process to be polled.
 *
 * Enqueues a POLL, coalescing with one already queued for the same target --
 * without that, a hot poll source would fill the queue with redundant POLLs and
 * starve other posters.  The scan and enqueue are one atomic section.
 *
 * @param p Process to poll
 */
void tiku_process_poll(struct tiku_process *p)
{
    uint8_t i;
    uint8_t idx;

    tiku_atomic_enter();

    /* Coalesce: drop the request if a POLL for this target is
     * already pending in the queue.  The coalesced return must STILL
     * wake the kernel thread (below): the pending POLL may have been
     * enqueued while the kernel was awake (wake was a no-op) and the
     * kernel blocked afterwards — without the wake here, a queued
     * event could sleep forever behind never-yielding workers. */
    for (i = 0; i < q_len; i++) {
        idx = (q_head + i) % TIKU_QUEUE_SIZE;
        if (queue[idx].ev == TIKU_EVENT_POLL && queue[idx].p == p) {
            tiku_atomic_exit();
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
            tiku_thread_kernel_wake();
#endif
            return;
        }
    }

    /* Inline the post so the scan and the enqueue happen under the
     * same critical section -- otherwise an ISR could slip a duplicate
     * POLL in between the scan and tiku_process_post().  POLL is a
     * system event, so it may use the reserved slots too. */
    if (q_len < TIKU_QUEUE_SIZE) {
        idx = (q_head + q_len) % TIKU_QUEUE_SIZE;
        queue[idx].ev   = TIKU_EVENT_POLL;
        queue[idx].data = NULL;
        queue[idx].p    = p;
        queue[idx].generation = event_generation(p);
        q_len++;
    } else {
        q_dropped++;
    }

    tiku_atomic_exit();

#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
    /* Same wake as tiku_process_post: a pending (possibly coalesced)
     * POLL is kernel work, so preempt any running worker. */
    tiku_thread_kernel_wake();
#endif
}

/*---------------------------------------------------------------------------*/
/* PRIVATE FUNCTIONS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Dispatch an event to a single process.
 *
 * Runs the thread and exits the process on PT_EXITED or PT_ENDED; FORCE_EXIT
 * bypasses the body entirely.  The return code is mapped to a state, so /proc
 * and `ps` distinguish WAITING from READY instead of collapsing both.
 *
 * @param p    Target process
 * @param ev   Event to deliver
 * @param data Associated event data
 */
static void call_process(struct tiku_process *p, tiku_event_t ev,
                         tiku_event_data_t data)
{
    char ret;

    if (p->is_running && p->thread) {
        if (ev == TIKU_EVENT_FORCE_EXIT) {
            if (tiku_current_process == p) {
                tiku_current_process = NULL;
            }
            p->exit_reason = (uint8_t)TIKU_EXIT_DONE;
            tiku_process_exit(p);
            return;
        }

        tiku_current_process = p;
        p->state = TIKU_PROCESS_STATE_RUNNING;
        p->wake_count++;
        ret = p->thread(&p->pt, ev, data);
        if (ret == PT_EXITED || ret == PT_ENDED) {
            /* Record how it ended: a clean protothread end is DONE unless the
             * process flagged itself FAILED (tiku_process_fail).  This is the
             * signal ON_FAILURE supervision keys on in tiku_process_exit(). */
            if (p->exit_reason != (uint8_t)TIKU_EXIT_FAILED) {
                p->exit_reason = (uint8_t)TIKU_EXIT_DONE;
            }
            tiku_current_process = NULL;
            tiku_process_exit(p);
        } else {
            /* Distinguish a voluntary yield (immediately runnable)
             * from a blocked wait (parked until an event arrives).
             * A blocked process that owns an armed timer is SLEEPING
             * (it has a scheduled wake-up); one with no timer is
             * WAITING (nothing will wake it but an external event).
             * PT_WAITING is the canonical blocked case, but unknown
             * return codes also fall through to the blocked branch
             * as the safer default. */
            if (ret == PT_YIELDED) {
                p->state = TIKU_PROCESS_STATE_READY;
            } else {
                p->state = tiku_timer_owner_armed(p)
                           ? TIKU_PROCESS_STATE_SLEEPING
                           : TIKU_PROCESS_STATE_WAITING;
            }
            tiku_current_process = NULL;
        }
    }
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Start all processes in a NULL-terminated array
 *
 * Iterates through the array and calls tiku_process_start() on
 * each process. Used by the scheduler to auto-start processes
 * registered with TIKU_AUTOSTART_PROCESSES().
 *
 * @param processes NULL-terminated array of process pointers
 */
void tiku_autostart_start(struct tiku_process * const processes[])
{
    int i;

    for (i = 0; processes[i] != NULL; i++) {
        if (tiku_hang_is_culprit(processes[i])) {
            /* One-shot quarantine: this process wedged the scheduler before
             * the last reset, so skip it on the recovery boot -- it can't
             * hang the board again from autostart.  The record is cleared for
             * the next boot, which starts it normally. */
            PROCESS_PRINTF("Quarantine: skipping %s (hung last boot)\n",
                           processes[i]->name);
            continue;
        }
        PROCESS_PRINTF("Autostart: %s\n", processes[i]->name);
        tiku_process_start(processes[i], NULL);
    }
}

/*---------------------------------------------------------------------------*/
/* QUEUE QUERY FUNCTIONS                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the number of free slots in the event queue
 *
 * @return Number of free slots
 */
uint8_t tiku_process_queue_space(void)
{
    uint8_t ret;

    tiku_atomic_enter();
    ret = (uint8_t)(TIKU_QUEUE_SIZE - q_len);
    tiku_atomic_exit();

    return ret;
}

/**
 * @brief Check if the event queue is full
 *
 * @return 1 if full, 0 otherwise
 */
uint8_t tiku_process_queue_full(void)
{
    uint8_t ret;

    tiku_atomic_enter();
    ret = (q_len == TIKU_QUEUE_SIZE);
    tiku_atomic_exit();

    return ret;
}

/**
 * @brief Check if the event queue is empty
 *
 * @return 1 if empty, 0 otherwise
 */
uint8_t tiku_process_queue_empty(void)
{
    uint8_t ret;

    tiku_atomic_enter();
    ret = (q_len == 0);
    tiku_atomic_exit();

    return ret;
}

/**
 * @brief Return the number of pending events in the queue
 *
 * @return Number of queued events
 */
uint8_t tiku_process_queue_length(void)
{
    uint8_t ret;

    tiku_atomic_enter();
    ret = q_len;
    tiku_atomic_exit();

    return ret;
}

/**
 * @brief Return the lifetime dropped-event count
 *
 * @return Events refused since boot because the queue (or the
 *         user-event budget) was full; wraps at 65535
 */
uint16_t tiku_process_queue_dropped(void)
{
    uint16_t ret;

    tiku_atomic_enter();
    ret = q_dropped;
    tiku_atomic_exit();

    return ret;
}

/**
 * @brief Peek at an event in the queue without removing it.
 *
 * Reads the event and/or target process at position @p index in the
 * pending event queue.  Index 0 is the head (next to be dispatched).
 * The queue itself is not modified.
 *
 * @param index   Zero-based position in the queue (0 .. queue_length-1).
 * @param ev      Output: event value at that position (may be NULL).
 * @param target  Output: target process pointer (may be NULL).
 * @return 0 on success, -1 if @p index is out of range.
 */
int8_t tiku_process_queue_peek(uint8_t index, tiku_event_t *ev,
                               struct tiku_process **target)
{
    uint8_t idx;

    tiku_atomic_enter();

    if (index >= q_len) {
        tiku_atomic_exit();
        return -1;
    }
    idx = (q_head + index) % TIKU_QUEUE_SIZE;
    if (ev != NULL) {
        *ev = queue[idx].ev;
    }
    if (target != NULL) {
        *target = queue[idx].p;
    }

    tiku_atomic_exit();

    return 0;
}

/**
 * @brief Check if a process is running
 *
 * @param p Process to check
 * @return 1 if running, 0 otherwise
 */
uint8_t tiku_process_is_running(struct tiku_process *p)
{
    return p->is_running;
}

/*---------------------------------------------------------------------------*/
/* PROCESS REGISTRY FUNCTIONS                                                */
/*---------------------------------------------------------------------------*/

/*
 * Lowercase names for the process-state enum, indexed directly by value -- so
 * the order MUST stay aligned with the enum.  Read only through
 * tiku_process_state_str(), for /proc and the `ps` command.
 */
static const char * const state_names[] = {
    "running",
    "ready",
    "waiting",
    "sleeping",
    "stopped"
};

/**
 * @brief Convert a process state enum to a printable string.
 *
 * Bounds-checked, so an out-of-range or corrupted value renders "unknown"
 * rather than reading past the table.  The pointer is to static storage.
 *
 * @param state  Process state value
 * @return Static lowercase name, or "unknown" if out of range
 */
const char *tiku_process_state_str(tiku_process_state_t state)
{
    if (state > TIKU_PROCESS_STATE_STOPPED) {
        return "unknown";
    }
    return state_names[state];
}

/**
 * @brief Register a process in the pid registry and start it.
 *
 * Idempotent on the pid.  Claims the first free slot, whose index becomes the
 * pid, then starts the process if it is not already running.  Process-context
 * only, and the list edit it triggers does its own locking.
 *
 * @param name  Human-readable name (NULL keeps the struct's name)
 * @param p     Process to register (NULL is rejected)
 * @return pid (0..TIKU_PROCESS_MAX-1) on success, -1 if @p p is NULL
 *         or the registry is full
 */
int8_t tiku_process_register(const char *name, struct tiku_process *p)
{
    uint8_t i;

    if (p == NULL) {
        return -1;
    }

    /* If the process already has a valid pid, return it */
    if (p->pid >= 0 && p->pid < TIKU_PROCESS_MAX &&
        registry[p->pid] == p) {
        return p->pid;
    }

    /* Find a free slot */
    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        if (registry[i] == NULL) {
            registry[i] = p;
            p->pid = (int8_t)i;
            if (name != NULL) {
                p->name = name;
            }
            /* Start the process if it is not already running */
            if (!p->is_running) {
                tiku_process_start(p, NULL);
            }
            return (int8_t)i;
        }
    }

    return -1;  /* registry full */
}

void tiku_process_attach_mem_arena(struct tiku_process *p,
                                   const void *arena)
{
    if (p != NULL) {
        p->mem_arena = arena;
    }
}

/** Measured bytes from the attached arena when its tier matches. */
static uint32_t process_measured(const struct tiku_process *p,
                                 int want_sram)
{
    const tiku_arena_t *a;
    tiku_mem_stats_t st;
    int is_sram;

    if (p == NULL || p->mem_arena == NULL) {
        return 0;
    }
    a = (const tiku_arena_t *)p->mem_arena;
    if (tiku_arena_stats(a, &st) != TIKU_MEM_OK) {
        return 0;
    }
    is_sram = (a->tier == TIKU_MEM_SRAM);
    if ((want_sram && is_sram) || (!want_sram && !is_sram)) {
        return (uint32_t)st.used_bytes;
    }
    return 0;
}

uint32_t tiku_process_sram_used(const struct tiku_process *p)
{
    if (p == NULL) {
        return 0;
    }
    return (uint32_t)p->sram_used + process_measured(p, 1);
}

uint32_t tiku_process_fram_used(const struct tiku_process *p)
{
    if (p == NULL) {
        return 0;
    }
    return (uint32_t)p->fram_used + process_measured(p, 0);
}

/**
 * @brief Look up a registered process by pid
 *
 * Pure array access with a bounds check; returns the slot contents,
 * which is NULL for a never-used pid.  Used throughout the registry
 * helpers, /proc, and the shell to resolve a pid to a control block.
 *
 * @param pid  Process identifier (0..TIKU_PROCESS_MAX-1)
 * @return Pointer to the process, or NULL if pid is out of range or
 *         the slot is empty
 */
struct tiku_process *tiku_process_get(int8_t pid)
{
    if (pid < 0 || pid >= TIKU_PROCESS_MAX) {
        return NULL;
    }
    return registry[pid];
}

/**
 * @brief Stop a registered process by pid.
 *
 * Marks it STOPPED and clears the running flag inside an atomic section, so a
 * concurrent post sees a consistent pair.  Unlike exit() it stays on the list
 * and broadcasts nothing, which is what lets resume() bring it back cheaply.
 *
 * @param pid  Process identifier
 * @return 0 on success, -1 if the pid does not resolve to a process
 */
int8_t tiku_process_stop(int8_t pid)
{
    struct tiku_process *p;

    p = tiku_process_get(pid);
    if (p == NULL) {
        return -1;
    }

    tiku_atomic_enter();
    p->state = TIKU_PROCESS_STATE_STOPPED;
    p->is_running = 0;
    tiku_atomic_exit();

    return 0;
}

/**
 * @brief Resume a previously stopped process by pid.
 *
 * STOPPED can come from either stop() or exit(), so the node may or may not
 * still be listed; this re-links only if absent, avoiding a cycle.  The
 * protothread state is intact, so it resumes where it yielded.
 *
 * @param pid  Process identifier
 * @return 0 on success, -1 if the pid is unknown or not STOPPED
 */
int8_t tiku_process_resume(int8_t pid)
{
    struct tiku_process *p;
    struct tiku_process *q;
    uint8_t in_list;

    p = tiku_process_get(pid);
    if (p == NULL || p->state != TIKU_PROCESS_STATE_STOPPED) {
        return -1;
    }

    tiku_atomic_enter();

    /* Check if process is still in the linked list */
    in_list = 0;
    for (q = tiku_process_list_head; q != NULL; q = q->next) {
        if (q == p) {
            in_list = 1;
            break;
        }
    }

    /* Re-add to list if it was removed (e.g., by tiku_process_exit) */
    if (!in_list) {
        p->next = tiku_process_list_head;
        tiku_process_list_head = p;
    }

    p->state = TIKU_PROCESS_STATE_READY;
    p->is_running = 1;

    tiku_atomic_exit();

    /* Post a CONTINUE event so the process wakes up */
    tiku_process_post(p, TIKU_EVENT_CONTINUE, NULL);

    return 0;
}

/**
 * @brief Count the occupied registry slots.
 *
 * The number of registered processes, STOPPED ones included since they keep
 * their slot -- so it can exceed the number currently on the active list.
 *
 * @return Number of registered processes (0..TIKU_PROCESS_MAX)
 */
uint8_t tiku_process_count(void)
{
    uint8_t i;
    uint8_t count = 0;

    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        if (registry[i] != NULL) {
            count++;
        }
    }
    return count;
}

/*---------------------------------------------------------------------------*/
/* PROCESS CATALOG                                                           */
/*---------------------------------------------------------------------------*/

/*
 * Catalog of available-but-not-started processes: a name-to-process directory
 * distinct from the pid registry, advertising only that something CAN be
 * started.  Populated at boot; process-context only.
 */
static tiku_process_catalog_entry_t catalog[TIKU_PROCESS_CATALOG_MAX];

/**
 * @brief Number of valid entries in catalog[]
 *
 * Grows as tiku_process_catalog_add() appends; never shrinks.
 * Zero-initialised as a BSS static at boot.
 */
static uint8_t catalog_count;

/**
 * @brief Byte-wise full-string equality test.
 *
 * A boolean strcmp() substitute, so small targets need not pull strcmp() in.
 * Equality requires identical length and content, so "net" does not match
 * "network".  Neither argument may be NULL.
 *
 * @param a  First NUL-terminated string
 * @param b  Second NUL-terminated string
 * @return 1 if the strings are identical, 0 otherwise
 */
static uint8_t
name_match(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == *b);
}

/**
 * @brief Add (or update) a catalog entry mapping a name to a process.
 *
 * Advertises the process without starting it; a matching name updates in place
 * rather than duplicating.  The name pointer is stored verbatim, not copied, so
 * it must have program lifetime.
 *
 * @param name  Human-readable name (e.g. "net", "mqtt"); NULL rejected
 * @param proc  Process struct to advertise; NULL rejected
 * @return 0 on success or update, -1 on NULL argument or full catalog
 */
int8_t
tiku_process_catalog_add(const char *name, struct tiku_process *proc)
{
    uint8_t i;

    if (name == NULL || proc == NULL) {
        return -1;
    }

    /* Check for duplicate */
    for (i = 0; i < catalog_count; i++) {
        if (name_match(catalog[i].name, name)) {
            catalog[i].proc = proc;
            return 0;
        }
    }

    if (catalog_count >= TIKU_PROCESS_CATALOG_MAX) {
        return -1;
    }

    catalog[catalog_count].name = name;
    catalog[catalog_count].proc = proc;
    catalog_count++;
    return 0;
}

/**
 * @brief Look up a catalog entry by name.
 *
 * A linear scan on exact equality, returning the advertised process so the
 * caller can start it.  Searches only the catalog -- use find_by_name() for the
 * live registry.
 *
 * @param name  Process name to search for (NULL yields NULL)
 * @return Pointer to the advertised process, or NULL if not catalogued
 */
struct tiku_process *
tiku_process_catalog_find(const char *name)
{
    uint8_t i;

    if (name == NULL) {
        return NULL;
    }

    for (i = 0; i < catalog_count; i++) {
        if (name_match(catalog[i].name, name)) {
            return catalog[i].proc;
        }
    }
    return NULL;
}

/**
 * @brief Return the number of catalog entries
 *
 * @return Current value of catalog_count (0..TIKU_PROCESS_CATALOG_MAX)
 */
uint8_t
tiku_process_catalog_count(void)
{
    return catalog_count;
}

/**
 * @brief Fetch a catalog entry by index.
 *
 * A bounds-checked accessor for iterating the catalog.  The pointer aliases the
 * internal table and stays valid while the catalog is unmodified; callers must
 * not write through it.
 *
 * @param idx  Index in [0, tiku_process_catalog_count())
 * @return Pointer to the entry, or NULL if @p idx is out of range
 */
const tiku_process_catalog_entry_t *
tiku_process_catalog_get(uint8_t idx)
{
    if (idx >= catalog_count) {
        return NULL;
    }
    return &catalog[idx];
}

/**
 * @brief Find a running or registered process by name.
 *
 * A linear scan of the pid registry, not the catalog, on exact equality.  It
 * finds processes that were actually registered, STOPPED ones included since
 * they keep their slot; nameless entries are skipped.
 *
 * @param name  Process name to search for (NULL yields NULL)
 * @return Pointer to the registered process, or NULL if none matches
 */
struct tiku_process *
tiku_process_find_by_name(const char *name)
{
    uint8_t i;

    if (name == NULL) {
        return NULL;
    }

    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        if (registry[i] != NULL && registry[i]->name != NULL &&
            name_match(registry[i]->name, name)) {
            return registry[i];
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/
/* CHANNEL FUNCTIONS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a channel
 *
 * @param ch       Channel to initialize
 * @param buf      Pointer to caller-provided storage
 * @param msg_size Size of each message in bytes
 * @param capacity Maximum number of messages
 */
void tiku_channel_init(struct tiku_channel *ch, void *buf,
                       uint8_t msg_size, uint8_t capacity)
{
    ch->buf      = (uint8_t *)buf;
    ch->msg_size = msg_size;
    ch->capacity = capacity;
    ch->head     = 0;
    ch->count    = 0;
}

/**
 * @brief Put a message into a channel
 *
 * Safe to call from interrupt context.
 *
 * @param ch  Channel to put message into
 * @param msg Pointer to message data (msg_size bytes copied)
 * @return 1 if message stored, 0 if channel is full
 */
uint8_t tiku_channel_put(struct tiku_channel *ch, const void *msg)
{
    uint8_t tail;

    tiku_atomic_enter();

    if (ch->count >= ch->capacity) {
        tiku_atomic_exit();
        return 0;
    }

    tail = (ch->head + ch->count) % ch->capacity;
    memcpy(&ch->buf[tail * ch->msg_size], msg, ch->msg_size);
    ch->count++;

    tiku_atomic_exit();
    return 1;
}

/**
 * @brief Get a message from a channel
 *
 * Safe to call from interrupt context.
 *
 * @param ch  Channel to read from
 * @param out Pointer to destination buffer (msg_size bytes copied)
 * @return 1 if a message was retrieved, 0 if channel is empty
 */
uint8_t tiku_channel_get(struct tiku_channel *ch, void *out)
{
    tiku_atomic_enter();

    if (ch->count == 0) {
        tiku_atomic_exit();
        return 0;
    }

    memcpy(out, &ch->buf[ch->head * ch->msg_size], ch->msg_size);
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    tiku_atomic_exit();
    return 1;
}

/**
 * @brief Check if a channel is empty
 *
 * @param ch Channel to check
 * @return 1 if empty, 0 otherwise
 */
uint8_t tiku_channel_is_empty(struct tiku_channel *ch)
{
    uint8_t ret;

    tiku_atomic_enter();
    ret = (ch->count == 0);
    tiku_atomic_exit();

    return ret;
}

/**
 * @brief Return the number of free slots in a channel
 *
 * @param ch Channel to check
 * @return Number of free message slots
 */
uint8_t tiku_channel_free(struct tiku_channel *ch)
{
    uint8_t ret;

    tiku_atomic_enter();
    ret = (uint8_t)(ch->capacity - ch->count);
    tiku_atomic_exit();

    return ret;
}
