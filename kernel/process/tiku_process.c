/*
 * Tiku Operating System v0.05
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

#include "tiku_process.h"
#include <kernel/memory/tiku_mem.h> /* measured accounting (attached arena) */
#include <hal/tiku_compiler.h>
#include <hal/tiku_cpu.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_timer.h>  /* tiku_timer_owner_armed (SLEEPING) */
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
#include <kernel/threads/tiku_thread.h> /* kernel_wake on every post */
#endif
#include <stddef.h>
#include <stdint.h>   /* uintptr_t for the typed-event payload accessors */
#include <string.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE TYPES                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief One slot in the event queue ring
 *
 * Captures a single posted event in flight between the poster
 * (tiku_process_post / tiku_process_poll, possibly an ISR) and the
 * dispatcher (tiku_process_run).  The target @p p is either a
 * specific process or TIKU_PROCESS_BROADCAST (NULL) for a fan-out to
 * every running process.  Field order is chosen so the wider data
 * pointer leads, which keeps the struct naturally packed on both the
 * 16-bit MSP430 and the 32-bit RP2350.
 */
struct event_item {
    tiku_event_data_t data;     /**< Opaque payload passed to the thread */
    struct tiku_process *p;     /**< Target process, or BROADCAST (NULL) */
    tiku_event_t ev;            /**< Event identifier (TIKU_EVENT_*) */
};

/*---------------------------------------------------------------------------*/
/* PRIVATE VARIABLES                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief The event queue ring buffer
 *
 * A fixed-size circular buffer of TIKU_QUEUE_SIZE event_item slots.
 * Written by tiku_process_post() / tiku_process_poll() (which an ISR
 * may invoke) and drained one entry per call by tiku_process_run().
 * The slots themselves are not declared volatile because every access
 * is bracketed by a tiku_atomic_enter()/exit() critical section that
 * serialises ISR and process-context use; only the head and length
 * indices below carry the volatile qualifier.
 */
static struct event_item queue[TIKU_QUEUE_SIZE];

/**
 * @brief Index of the oldest pending event (the next to dispatch)
 *
 * Advances by one (mod TIKU_QUEUE_SIZE) each time tiku_process_run()
 * dequeues an event.  volatile because an ISR posting through
 * tiku_process_post() reads it to compute the tail slot.  Only ever
 * mutated inside an atomic section.
 */
static volatile uint8_t q_head = 0;

/**
 * @brief Number of events currently in the ring
 *
 * Ranges 0..TIKU_QUEUE_SIZE.  The tail slot is derived as
 * (q_head + q_len) % TIKU_QUEUE_SIZE, so head + length fully describe
 * the ring without a separate tail index.  Incremented on post,
 * decremented on dispatch; volatile and atomic-section guarded for
 * the same ISR-vs-process reason as q_head.
 */
static volatile uint8_t q_len = 0;

/**
 * @brief Lifetime count of dropped events (queue full at post time)
 *
 * Bumped inside the same atomic section as the failed enqueue, so it
 * is exact.  Never reset except by tiku_process_init(); wraps at
 * 65535.  Surfaced at /proc/queue/dropped — the observability for a
 * failure mode that used to be completely silent.
 */
static volatile uint16_t q_dropped = 0;

/**
 * @brief Is @p ev a kernel/system event (vs an application event)?
 *
 * System events occupy the low control range (INIT..FORCE_EXIT,
 * below TIKU_EVENT_USER) and the high kernel range (TIMER and up).
 * Application events live in [TIKU_EVENT_USER, TIKU_EVENT_TIMER).
 * The distinction feeds the TIKU_QUEUE_RESERVE admission check in
 * tiku_process_post().
 */
static inline uint8_t event_is_system(tiku_event_t ev)
{
    return (ev < TIKU_EVENT_USER) || (ev >= TIKU_EVENT_TIMER);
}

/**
 * @brief Process registry — fixed array indexed by pid
 *
 * Slot i holds the process whose pid == i, or NULL when free.
 * Capacity is TIKU_PROCESS_MAX (8).  Populated by
 * tiku_process_register(), cleared back to NULL by
 * tiku_process_init().  Note that stopping a process
 * (tiku_process_stop / exit) does not vacate its registry slot — the
 * pid stays reserved so /proc and the shell can still inspect a
 * stopped process and resume it.  Touched only from process context.
 */
static struct tiku_process *registry[TIKU_PROCESS_MAX];

/*---------------------------------------------------------------------------*/
/* PUBLIC VARIABLES                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Head of the singly-linked list of started processes
 *
 * Every process with is_running != 0 is linked here through its
 * ->next field; tiku_process_run() walks this list to fan out a
 * broadcast event.  Insertion (start/resume) and removal (exit) are
 * performed inside atomic sections because a broadcast post from an
 * ISR could otherwise observe a half-linked node.  Process context
 * is the only reader.
 */
struct tiku_process *tiku_process_list_head = NULL;

/**
 * @brief The process whose thread is currently executing
 *
 * Set by call_process() for the duration of a single thread
 * invocation and cleared back to NULL on return.  The TIKU_THIS() /
 * TIKU_LOCAL() macros read it so a thread body can reach its own
 * control block and local storage without being passed a self
 * pointer.  Because dispatch is cooperative and single-threaded this
 * never needs locking; it is meaningful only between the enter and
 * exit of one thread call.
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
 * @brief Start a process
 *
 * Idempotent: if the process is already running this returns
 * immediately, so double-start is harmless.  Otherwise it
 * reinitialises the protothread state with PT_INIT(), links the
 * process at the head of tiku_process_list_head, marks it READY, and
 * stamps start_time from the current clock tick while zeroing
 * wake_count for fresh observability counters.  The link edit runs
 * inside an atomic section because a broadcast event posted from an
 * ISR walks the list and must never see a partially-linked node.
 *
 * After the critical section the INIT event is delivered.  The post
 * may fail only if the 32-slot event queue is full; in that case the
 * thread is run synchronously via call_process() so INIT is never
 * silently dropped — a started process is guaranteed exactly one
 * INIT.  This does NOT register the process in the pid registry;
 * tiku_process_register() does that and then calls this.
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
     * while we are linking a new node into the list. */
    tiku_atomic_enter();

    PT_INIT(&p->pt);

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
 * @brief Exit a process
 *
 * No-op if the process is not running.  Clears is_running, sets the
 * state to STOPPED, and unlinks the process from
 * tiku_process_list_head (handling both the head case and the
 * mid-list case via a predecessor scan).  The unlink runs inside an
 * atomic section for the same reason as start: an ISR broadcast walk
 * must not trip over a node being removed.
 *
 * After unlinking, a TIKU_EVENT_EXITED is broadcast to all surviving
 * processes with the exiting process pointer as the event data, so
 * subsystems that hold per-process resources (notably the timer
 * process) can release anything they own on its behalf.  The exiting
 * process keeps its registry slot and pid — only the linked-list
 * membership and is_running flag are cleared here, so the process can
 * still be inspected and later resumed by pid.
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
 * @brief Post an event to a process
 *
 * Appends one event_item to the tail of the ring buffer.  The tail
 * slot is computed as (q_head + q_len) % TIKU_QUEUE_SIZE, so no
 * separate tail index is kept.  When the queue is full (q_len ==
 * TIKU_QUEUE_SIZE) the event is dropped and 0 is returned; the
 * caller decides how to recover (tiku_process_start(), for example,
 * falls back to a synchronous dispatch).
 *
 * Safe to call from interrupt context: the read-modify-write of the
 * ring is wrapped in a tiku_atomic_enter()/exit() pair, which on the
 * outermost level masks interrupts and restores the prior interrupt
 * state on exit.  This does not itself run the target thread —
 * delivery happens later when tiku_process_run() drains the queue
 * from process context.  TIKU_PROCESS_BROADCAST (NULL) is stored
 * verbatim and expanded to a fan-out at dispatch time.
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

uint32_t tiku_event_u32(tiku_event_t ev, tiku_event_data_t data)
{
    return (tiku_event_payload_kind(ev) == TIKU_EVENT_PAYLOAD_U32)
               ? (uint32_t)(uintptr_t)data
               : 0u;
}

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
 * @brief Run the process scheduler
 *
 * Dequeues exactly one event from the head of the ring and
 * dispatches it, then returns.  The main loop calls this repeatedly
 * and drops to a low-power mode when it returns 0 (queue empty),
 * relying on an ISR posting a new event to wake the CPU.
 *
 * The dequeue (reading head fields, advancing q_head, decrementing
 * q_len) is done inside an atomic section so it cannot race an
 * ISR-side post.  Dispatch deliberately happens AFTER leaving the
 * atomic section: running a thread can take many cycles, and holding
 * interrupts off across it would inflate interrupt latency.  A
 * broadcast target (TIKU_PROCESS_BROADCAST) is fanned out to every
 * process on tiku_process_list_head; the next pointer is cached
 * before each call so a process that exits itself mid-dispatch does
 * not corrupt the walk.  A unicast target is delivered directly.
 *
 * Note this delivers one queued event per call, not one per process;
 * fairness across processes therefore follows queue (FIFO) order.
 *
 * @return 1 if an event was processed, 0 if idle
 */
uint8_t tiku_process_run(void)
{
    tiku_event_t ev;
    tiku_event_data_t data;
    struct tiku_process *receiver;

    tiku_atomic_enter();

    if (q_len == 0) {
        tiku_atomic_exit();
        return 0;
    }

    ev = queue[q_head].ev;
    data = queue[q_head].data;
    receiver = queue[q_head].p;
    q_head = (q_head + 1) % TIKU_QUEUE_SIZE;
    q_len--;

    tiku_atomic_exit();

    /* Dispatch outside atomic section to avoid long interrupt latency */
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
 * Identical to tiku_process_run() except events destined for @p skip are
 * consumed without dispatch (and @p skip is left out of a broadcast fan-out).
 * The one use is a synchronous, long-running op running INSIDE @p skip's own
 * dispatch (e.g. BASIC's blocking HTTPGET$ while it drives a crypto worker):
 * it may pump the rest of the kernel's processes so timers and rules keep
 * firing, but must not recursively re-enter its own protothread, whose saved
 * PT state points at the last yield, not the deep C call it is parked in.
 * Directed events to @p skip during that window are dropped (a POLL
 * regenerates; the caller is by definition already awake and running).
 *
 * @param skip Process not to dispatch (typically TIKU_THIS()); NULL == plain run
 * @return 1 if an event was dequeued (dispatched or skipped), 0 if queue empty
 */
uint8_t tiku_process_run_except(const struct tiku_process *skip)
{
    tiku_event_t ev;
    tiku_event_data_t data;
    struct tiku_process *receiver;

    tiku_atomic_enter();

    if (q_len == 0) {
        tiku_atomic_exit();
        return 0;
    }

    ev = queue[q_head].ev;
    data = queue[q_head].data;
    receiver = queue[q_head].p;
    q_head = (q_head + 1) % TIKU_QUEUE_SIZE;
    q_len--;

    tiku_atomic_exit();

    if (receiver == skip) {
        return 1;                        /* consume without re-entering skip */
    }
    if (receiver == TIKU_PROCESS_BROADCAST) {
        struct tiku_process *p, *next;
        for (p = tiku_process_list_head; p != NULL; p = next) {
            next = p->next;
            if (p != skip) {
                call_process(p, ev, data);
            }
        }
    } else {
        call_process(receiver, ev, data);
    }

    return 1;
}

/**
 * @brief Request a process to be polled
 *
 * Enqueues a TIKU_EVENT_POLL targeted at @p p so the scheduler will
 * dispatch it on the next pass.  Pending POLL events are coalesced:
 * if a POLL for the same target is already in the queue, this call
 * is a no-op.  Without that de-duplication, a hot poll source (for
 * example a periodic timer ISR or a chatty interrupt-driven driver)
 * could fill the 32-slot event queue with redundant POLLs and starve
 * other event posters.
 *
 * The scan-and-enqueue runs inside a single atomic section so the
 * function stays safe to call from interrupt context.  Worst-case
 * cost is O(TIKU_QUEUE_SIZE) compares per call, which is bounded
 * (32 by default) and acceptable for poll-rate sources.
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
 * @brief Dispatch an event to a single process
 *
 * Runs the process thread and handles automatic exit when the thread
 * returns PT_EXITED, PT_ENDED, or receives TIKU_EVENT_FORCE_EXIT.
 *
 * The post-dispatch state is classified by the protothread return code
 * so /proc and `ps` show an accurate picture instead of collapsing
 * every non-exit return into WAITING:
 *
 *   PT_WAITING -> WAITING -- thread is blocked on a PT_WAIT_UNTIL
 *                            condition or an event it has not yet
 *                            received; will be re-scheduled when an
 *                            event for it arrives
 *   PT_YIELDED -> READY   -- thread voluntarily yielded via PT_YIELD;
 *                            it is immediately runnable on the next
 *                            scheduler pass
 *   PT_EXITED  -> STOPPED -- handled via tiku_process_exit() below
 *   PT_ENDED   -> STOPPED -- ditto
 *
 * SLEEPING is reserved for explicit timer-driven blocking once the
 * timer subsystem feeds back wake-up state into the process record.
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
        tiku_current_process = p;
        p->state = TIKU_PROCESS_STATE_RUNNING;
        p->wake_count++;
        ret = p->thread(&p->pt, ev, data);
        if (ret == PT_EXITED || ret == PT_ENDED ||
            ev == TIKU_EVENT_FORCE_EXIT) {
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
    return TIKU_QUEUE_SIZE - q_len;
}

/**
 * @brief Check if the event queue is full
 *
 * @return 1 if full, 0 otherwise
 */
uint8_t tiku_process_queue_full(void)
{
    return q_len == TIKU_QUEUE_SIZE;
}

/**
 * @brief Check if the event queue is empty
 *
 * @return 1 if empty, 0 otherwise
 */
uint8_t tiku_process_queue_empty(void)
{
    return q_len == 0;
}

/**
 * @brief Return the number of pending events in the queue
 *
 * @return Number of queued events
 */
uint8_t tiku_process_queue_length(void)
{
    return q_len;
}

/**
 * @brief Return the lifetime dropped-event count
 *
 * @return Events refused since boot because the queue (or the
 *         user-event budget) was full; wraps at 65535
 */
uint16_t tiku_process_queue_dropped(void)
{
    return q_dropped;
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
    if (index >= q_len) {
        return -1;
    }
    uint8_t idx = (q_head + index) % TIKU_QUEUE_SIZE;
    if (ev != NULL) {
        *ev = queue[idx].ev;
    }
    if (target != NULL) {
        *target = queue[idx].p;
    }
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

/**
 * @brief Lowercase names for the tiku_process_state_t enum
 *
 * Indexed directly by the enum value, so the order MUST stay aligned
 * with tiku_process_state_t (RUNNING, READY, WAITING, SLEEPING,
 * STOPPED) in tiku_process.h.  Read only through
 * tiku_process_state_str(); consumed by /proc nodes and the shell
 * "ps" command for human-readable state output.
 */
static const char * const state_names[] = {
    "running",
    "ready",
    "waiting",
    "sleeping",
    "stopped"
};

/**
 * @brief Convert a process state enum to a printable string
 *
 * Bounds-checks @p state against the last enumerator and indexes
 * state_names[].  Any out-of-range value (including a corrupted
 * record) renders as "unknown" rather than reading past the table.
 * The returned pointer is to static storage and must not be freed.
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
 * @brief Register a process in the pid registry and start it
 *
 * Idempotent on the pid: if @p p already occupies its recorded slot
 * (pid valid and registry[pid] == p) the existing pid is returned and
 * nothing else happens, so calling this twice is safe.  Otherwise the
 * first free registry slot is claimed, its index becomes the pid, and
 * the optional @p name overrides the struct's name field.  If the
 * process is not already running it is then started via
 * tiku_process_start(), which links it into the active list and
 * delivers INIT — so on first registration the start_time and
 * wake_count counters are established here transitively.
 *
 * Runs in process context (typically at boot or from the shell
 * "start" path); it does not enter an atomic section itself because
 * the registry array is only ever touched from process context, while
 * the list edit it triggers inside tiku_process_start() does its own
 * locking.
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
 * @brief Stop a registered process by pid
 *
 * Marks the process STOPPED and clears is_running so call_process()
 * skips it on every subsequent dispatch (a STOPPED process never
 * runs its thread).  The two field writes are wrapped in an atomic
 * section so a concurrent ISR post or scheduler pass sees a
 * consistent (state, is_running) pair.
 *
 * Unlike tiku_process_exit(), this does NOT unlink the process from
 * tiku_process_list_head and does NOT broadcast EXITED — the node
 * stays on the list, just inert.  tiku_process_resume() relies on
 * that to bring it back cheaply, and the still-present registry slot
 * keeps the pid valid for inspection.
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
 * @brief Resume a previously stopped process by pid
 *
 * Only acts on a process currently in the STOPPED state; any other
 * state (or an unknown pid) is rejected with -1.  Because either
 * tiku_process_stop() or tiku_process_exit() may have produced the
 * STOPPED state, the process might or might not still be on the
 * active list, so this first scans tiku_process_list_head and
 * re-links the node at the head only if it is absent — avoiding a
 * double-link that would create a cycle.  The list check, optional
 * re-link, and the (state, is_running) update all run inside one
 * atomic section.
 *
 * Note the protothread state (p->pt) is left intact, so the thread
 * resumes from where it yielded rather than restarting.  A
 * TIKU_EVENT_CONTINUE is posted afterwards so the scheduler actually
 * dispatches the process again; if that post is dropped because the
 * queue is full the process is runnable but will not wake until some
 * other event targets it.
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
 * @brief Count the occupied registry slots
 *
 * Counts non-NULL entries across the whole registry.  This is the
 * number of registered processes, which includes ones that are
 * currently STOPPED — a stopped process keeps its slot — so it may
 * exceed the number of processes presently on the active list.
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

/**
 * @brief Catalog of available-but-not-yet-started processes
 *
 * Name-to-process directory, distinct from the pid registry: a
 * catalog entry only advertises that a process *can* be started (by
 * the shell "start" command or the init system), it does not run it.
 * Subsystems populate it at boot via tiku_process_catalog_add().
 * Holds up to TIKU_PROCESS_CATALOG_MAX entries; entries [0,
 * catalog_count) are valid.  Process-context only.
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
 * @brief Byte-wise full-string equality test
 *
 * A minimal strcmp() substitute that returns a boolean rather than an
 * ordering, used so small targets need not pull strcmp() into the
 * image.  Walks both strings in lockstep; on the first differing byte
 * returns 0.  When one string ends first the trailing comparison
 * (*a == *b) is false unless BOTH terminate together, so "net" does
 * not match "network" — equality requires identical length and
 * content.  Neither argument may be NULL.
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
 * @brief Add (or update) a catalog entry mapping a name to a process
 *
 * Advertises @p proc under @p name without starting it.  If an entry
 * with a matching name already exists its process pointer is replaced
 * in place and 0 is returned (so re-advertising under the same name
 * is an update, not a duplicate).  Otherwise a new entry is appended
 * and catalog_count grows.  The name pointer is stored verbatim, not
 * copied, so it must point to storage with program lifetime (a string
 * literal or a long-lived buffer).
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
 * @brief Look up a catalog entry by name
 *
 * Linear scan of the catalog using name_match() (exact, full-length
 * equality).  Returns the advertised process struct so the caller can
 * start it; this does not start anything itself.  Searches only the
 * catalog, not the active pid registry — use
 * tiku_process_find_by_name() for the latter.
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
 * @brief Fetch a catalog entry by index
 *
 * Bounds-checked accessor for iterating the catalog (e.g. a shell
 * "start" command listing available processes).  The returned pointer
 * aliases the internal table and stays valid as long as the catalog
 * is not modified; callers must not write through it.
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
 * @brief Find a running/registered process by name
 *
 * Linear scan of the pid registry (NOT the catalog) for a slot whose
 * process has a matching name, compared with name_match() (exact,
 * full-length).  This finds processes that have actually been
 * registered/started, including ones currently STOPPED that still
 * hold their slot.  Entries with a NULL name are skipped.
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
    return ch->count == 0;
}

/**
 * @brief Return the number of free slots in a channel
 *
 * @param ch Channel to check
 * @return Number of free message slots
 */
uint8_t tiku_channel_free(struct tiku_channel *ch)
{
    return ch->capacity - ch->count;
}