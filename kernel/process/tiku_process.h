/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_process.h - Process management for event-driven cooperative multitasking
 *
 * Provides protothread-based process management with an event queue for
 * inter-process communication. Processes run cooperatively and communicate
 * via posted events.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PROCESS_H_
#define TIKU_PROCESS_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"
#include "tiku_proto.h"
#include <kernel/timers/tiku_clock.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Event queue size (power of 2 for fast modulo) */
#define TIKU_QUEUE_SIZE         32

/**
 * @brief Queue slots reserved for system events.
 *
 * User-range posts stop at TIKU_QUEUE_SIZE minus this reserve, so these slots
 * stay open to system events (TIMER, EXITED, INIT, VFS, GPIO) when user posts
 * fill the rest.
 */
#define TIKU_QUEUE_RESERVE      4

/** @brief Maximum number of processes in the registry */
#define TIKU_PROCESS_MAX        8

/** @brief Post an event to all running processes */
#define TIKU_PROCESS_BROADCAST  NULL

/*---------------------------------------------------------------------------*/
/* SYSTEM EVENTS                                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_EVENT_INIT         0x01  /**< Started; data is the start arg */
#define TIKU_EVENT_EXIT         0x02  /**< Exit request the process acts on */
#define TIKU_EVENT_CONTINUE     0x03  /**< Resumed by tiku_process_resume() */
#define TIKU_EVENT_POLL         0x04  /**< Queued by tiku_process_poll() */
#define TIKU_EVENT_EXITED       0x05  /**< Exited; data is the process */
#define TIKU_EVENT_FORCE_EXIT   0x06  /**< Exit without running the body */
#define TIKU_EVENT_USER         0x10  /**< First application event id */
#define TIKU_EVENT_TIMER        0x88  /**< Timer expired; data is the timer */
#define TIKU_EVENT_GPIO         0x89  /**< GPIO edge; data packs port/pin */
#define TIKU_EVENT_VFS          0x8A  /**< Watched VFS node changed — data
                                           carries the const tiku_vfs_node_t*
                                           (see tiku_vfs_watch()) */

/** @brief Return code for successful process operations */
#define TIKU_PROCESS_ERR_OK     0

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Event identifier (TIKU_EVENT_*). */
typedef uint8_t tiku_event_t;
/** @brief Payload word posted with an event; see tiku_event_payload_kind(). */
typedef void *tiku_event_data_t;

/*---------------------------------------------------------------------------*/
/* TYPED EVENT PAYLOADS                                                      */
/*---------------------------------------------------------------------------*/
/*
 * The event data is a void*, and each event id fixes its payload type:
 * EXITED a process, VFS a node, TIMER a timer, GPIO a packed pin, INIT and
 * USER an application pointer.  tiku_event_payload_kind() maps an id to its
 * type, and each checked accessor below returns NULL or 0 for an id that
 * carries another type.
 *
 * The vfs and timer types are forward-declared and only cast here; this
 * header includes neither module's header.
 */
struct tiku_vfs_node;
struct tiku_timer;

/** @brief Payload type an event id carries. */
typedef enum {
    TIKU_EVENT_PAYLOAD_NONE = 0,  /**< EXIT/CONTINUE/POLL/FORCE_EXIT: no data */
    TIKU_EVENT_PAYLOAD_PROC,      /**< struct tiku_process*   (EXITED)        */
    TIKU_EVENT_PAYLOAD_NODE,      /**< const tiku_vfs_node_t* (VFS)           */
    TIKU_EVENT_PAYLOAD_TIMER,     /**< struct tiku_timer*     (TIMER)         */
    TIKU_EVENT_PAYLOAD_U32,       /**< packed small integer   (GPIO)          */
    TIKU_EVENT_PAYLOAD_PTR        /**< opaque app pointer     (INIT, USER)    */
} tiku_event_payload_kind_t;

/** @brief Return the payload type event @p ev carries. */
tiku_event_payload_kind_t tiku_event_payload_kind(tiku_event_t ev);

/*
 * Checked payload accessors.  Each returns the payload when @p ev carries
 * that type, and NULL or 0 otherwise.
 */

/**
 * @brief Checked accessor for the process payload of an event.
 *
 * Returns @p data as a process for TIKU_EVENT_EXITED, the one id whose
 * payload is PROC, and NULL for any other id.  Reads no state, so an ISR
 * may call it.
 *
 * @param ev   Event identifier as delivered to the thread
 * @param data Raw payload word delivered alongside @p ev
 * @return The process carried by @p ev, or NULL if @p ev carries no
 *         PROC payload
 */
struct tiku_process        *tiku_event_proc (tiku_event_t ev, tiku_event_data_t data);

/**
 * @brief Checked accessor for the VFS-node payload of an event.
 *
 * Returns @p data as the watched node for TIKU_EVENT_VFS, the one id whose
 * payload is NODE, and NULL for any other id.  Reads no state, so an ISR
 * may call it.
 *
 * @param ev   Event identifier as delivered to the thread
 * @param data Raw payload word delivered alongside @p ev
 * @return The node that changed, or NULL if @p ev carries no NODE
 *         payload
 */
const struct tiku_vfs_node *tiku_event_node (tiku_event_t ev, tiku_event_data_t data);

/**
 * @brief Checked accessor for the software-timer payload of an event.
 *
 * Returns @p data as the expired timer for TIKU_EVENT_TIMER and NULL for any
 * other id.  Reads no state, so an ISR may call it.
 *
 * @param ev   Event identifier as delivered to the thread
 * @param data Raw payload word delivered alongside @p ev
 * @return The tiku_timer that expired, or NULL if @p ev carries no
 *         TIMER payload
 */
struct tiku_timer          *tiku_event_timer(tiku_event_t ev, tiku_event_data_t data);

/**
 * @brief Checked accessor for the packed integer payload of an event.
 *
 * Returns @p data as an integer for TIKU_EVENT_GPIO, the one id whose
 * payload is U32 (its word packs the port and pin), and 0 for any other id.
 *
 * @param ev   Event identifier as delivered to the thread
 * @param data Raw payload word delivered alongside @p ev
 * @return The packed value, or 0 if @p ev carries no U32 payload
 */
uint32_t                    tiku_event_u32  (tiku_event_t ev, tiku_event_data_t data);

/**
 * @brief Checked accessor for the opaque app pointer of an event.
 *
 * Returns @p data for the ids whose payload is PTR, TIKU_EVENT_INIT and the
 * user range (TIKU_EVENT_USER .. TIKU_EVENT_TIMER-1), and NULL for any other
 * id.  Reads no state, so an ISR may call it.
 *
 * @param ev   Event identifier as delivered to the thread
 * @param data Raw payload word delivered alongside @p ev
 * @return The application pointer, or NULL if @p ev carries no PTR
 *         payload
 */
void                       *tiku_event_ptr  (tiku_event_t ev, tiku_event_data_t data);

/**
 * @brief Post @p ev carrying a process payload (EXITED).
 *
 * Posts @p arg as the data word, which tiku_event_proc() reads back.
 * tiku_process_post() posts the NONE and PTR events.
 *
 * @return 1 if posted, 0 if the queue is full or the memory-reclaim gate
 *         refuses the event for the target (TIKU_MEM_RECLAIM_ENABLE)
 */
uint8_t tiku_process_post_proc(struct tiku_process *dest, tiku_event_t ev,
                               struct tiku_process *arg);

/**
 * @brief Post @p ev carrying a VFS node payload (VFS).
 * @return 1 if posted, 0 if the queue is full or the memory-reclaim gate
 *         refuses the event for the target (TIKU_MEM_RECLAIM_ENABLE)
 */
uint8_t tiku_process_post_node(struct tiku_process *dest, tiku_event_t ev,
                               const struct tiku_vfs_node *node);

/**
 * @brief Process state for observability
 */
typedef enum {
    TIKU_PROCESS_STATE_RUNNING  = 0,  /**< its thread is executing now */
    TIKU_PROCESS_STATE_READY    = 1,  /**< started, resumed or yielded */
    TIKU_PROCESS_STATE_WAITING  = 2,  /**< blocked, owns no armed timer */
    TIKU_PROCESS_STATE_SLEEPING = 3,  /**< blocked, owns an armed timer */
    TIKU_PROCESS_STATE_STOPPED  = 4   /**< stopped or exited */
} tiku_process_state_t;

/**
 * @brief Per-process restart policy (supervision).
 *
 * tiku_process_exit() consults this to decide whether to bring a fresh
 * instance straight back under the same pid.  NEVER is the default.
 */
typedef enum {
    TIKU_RESTART_NEVER      = 0,  /**< never auto-restarted (default) */
    TIKU_RESTART_ON_FAILURE = 1,  /**< restarted only after a FAILED exit */
    TIKU_RESTART_ALWAYS     = 2   /**< restarted on any exit (a service) */
} tiku_restart_policy_t;

/**
 * @brief How a process ended -- the signal ON_FAILURE supervision keys on.
 *
 * A clean protothread end (TIKU_PROCESS_END() or TIKU_PROCESS_EXIT()) is DONE;
 * a process marks itself FAILED via tiku_process_fail() before it ends.  NONE
 * while running.
 */
typedef enum {
    TIKU_EXIT_NONE   = 0,  /**< running / never exited */
    TIKU_EXIT_DONE   = 1,  /**< ended normally (end or exit) */
    TIKU_EXIT_FAILED = 2,  /**< failed (tiku_process_fail) */
    TIKU_EXIT_RECLAIM = 3  /**< exit a memory-reclaim job asked for and
                                validated; not a failure */
} tiku_exit_reason_t;

/*---------------------------------------------------------------------------*/
/* PROCESS STRUCTURE                                                         */
/*---------------------------------------------------------------------------*/

struct tiku_process;

/**
 * @brief Process control block
 *
 * Contains all state needed to manage a cooperative process including
 * its protothread state, linkage in the process list, and run status.
 */
typedef struct tiku_process {
    struct tiku_process *next;      /**< Next process in linked list */
    const char *name;               /**< Human-readable process name */
    PT_THREAD((*thread)(struct pt *,
        tiku_event_t,
        tiku_event_data_t));        /**< Process thread function */
    struct pt pt;                   /**< Protothread control state */
    uint8_t is_running;             /**< Non-zero if process is active */
    uint8_t generation;             /**< Fresh-instance tag for queued events */
    void *local;                    /**< Per-process local state (a static
                                         struct of the caller's), or NULL */
    /* --- Observability fields --- */
    tiku_process_state_t state;     /**< Current process state */
    int8_t pid;                     /**< Registry index (-1 = unregistered) */
    uint16_t sram_used;             /**< Self-declared SRAM bytes (advisory) */
    uint16_t fram_used;             /**< Self-declared bytes outside SRAM
                                         (advisory) */
    tiku_clock_time_t start_time;   /**< Tick count when process started */
    uint16_t wake_count;            /**< Thread calls since the last start */
    const void *mem_arena;          /**< Attached tiku_arena_t or NULL;
                                         tiku_process_sram/fram_used() add
                                         its use to the declared figure */
    /* --- Supervision (per-process restart policy) --- */
    uint8_t  restart;               /**< tiku_restart_policy_t; 0 = NEVER */
    uint8_t  exit_reason;           /**< tiku_exit_reason_t; set at exit */
    uint8_t  restart_burst;         /**< restarts in the current window */
    uint16_t restart_total;         /**< lifetime restarts */
    tiku_clock_time_t restart_at;   /**< tick of the last restart */
    tiku_event_data_t init_data;    /**< INIT payload, replayed on restart */
} tiku_process_t;

/*---------------------------------------------------------------------------*/
/* CHANNEL STRUCTURE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Channel control block
 *
 * A ring of fixed-size messages in caller-provided storage.  Put and get run
 * in atomic sections, so an ISR may call them; TIKU_CHANNEL_DECLARE() adds
 * typed accessors.
 */
typedef struct tiku_channel {
    uint8_t *buf;               /**< Pointer to message storage */
    uint8_t  msg_size;          /**< Size of each message in bytes */
    uint8_t  capacity;          /**< Maximum number of messages */
    volatile uint8_t head;      /**< Index of oldest message */
    volatile uint8_t count;     /**< Number of messages in channel */
} tiku_channel_t;

/*---------------------------------------------------------------------------*/
/* MESSAGE STRUCTURE                                                         */
/*---------------------------------------------------------------------------*/

/** @brief Message header: a type tag and the payload length. */
struct tiku_msg {
    uint8_t type;      /**< Which payload struct follows the header */
    uint8_t len;       /**< Payload size in bytes */
};

/*---------------------------------------------------------------------------*/
/* PROCESS DECLARATION MACROS                                                */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_PROCESS(proc, strname)
 * @brief Declare and define a process (no local storage)
 *
 * The local pointer is NULL; TIKU_PROCESS_WITH_LOCAL() declares a process
 * with storage that outlives its yields.
 *
 * @param proc     Process variable name
 * @param strname  Human-readable name string
 */
#define TIKU_PROCESS(proc, strname)                                         \
    TIKU_PROCESS_THREAD(proc, ev, data);                                    \
    struct tiku_process proc = {                                             \
        .next = NULL,                                                        \
        .name = strname,                                                     \
        .thread = tiku_process_thread_##proc,                                \
        .pt = {0},                                                           \
        .is_running = 0,                                                     \
        .generation = 0,                                                     \
        .local = NULL,                                                       \
        .state = TIKU_PROCESS_STATE_STOPPED,                                 \
        .pid = -1,                                                           \
    }

/**
 * @def TIKU_PROCESS_WITH_LOCAL(proc, strname, local_type)
 * @brief Declare and define a process with typed local storage
 *
 * Allocates a static instance of local_type and wires the pointer into the
 * process struct at compile time.  The storage is BSS, so a restarted
 * process sees its previous values unless the thread body clears them.
 *
 * @param proc       Process variable name
 * @param strname    Human-readable name string
 * @param local_type The struct type for local storage
 */
#define TIKU_PROCESS_WITH_LOCAL(proc, strname, local_type)                  \
    TIKU_PROCESS_THREAD(proc, ev, data);                                    \
    static local_type tiku_local_##proc;                                    \
    struct tiku_process proc = {                                             \
        .next = NULL,                                                        \
        .name = strname,                                                     \
        .thread = tiku_process_thread_##proc,                                \
        .pt = {0},                                                           \
        .is_running = 0,                                                     \
        .generation = 0,                                                     \
        .local = &tiku_local_##proc,                                         \
        .state = TIKU_PROCESS_STATE_STOPPED,                                 \
        .pid = -1,                                                           \
    }

/**
 * @def TIKU_PROCESS_THREAD(name, ev, data)
 * @brief Declare a process thread function
 *
 * Declares the static protothread tiku_process_thread_<name>, whose
 * parameters are process_pt, @p ev and @p data.
 */
#define TIKU_PROCESS_THREAD(name, ev, data)                                \
    static PT_THREAD(tiku_process_thread_##name(                           \
        struct pt *process_pt, tiku_event_t ev,                            \
        tiku_event_data_t data))

/**
 * @def TIKU_LOCAL(type)
 * @brief Access the current process's local storage with a typed cast
 *
 * The cast is unchecked; TIKU_PROCESS_TYPED() generates a typed accessor.
 *
 * @param type The struct type of the local storage
 * @return Pointer to the typed local storage
 * @note Valid only inside a process thread body, placed above
 *       TIKU_PROCESS_BEGIN() so it re-runs on every entry.
 */
#define TIKU_LOCAL(type) ((type *)TIKU_THIS()->local)

/**
 * @def TIKU_PROCESS_TYPED(name, strname, local_type)
 * @brief Declare a process with a per-process type-safe accessor
 *
 * Like TIKU_PROCESS_WITH_LOCAL, plus a generated inline name_local() that
 * returns a correctly typed pointer with no cast at the call site.
 *
 * @param name       Process variable name
 * @param strname    Human-readable name string
 * @param local_type The struct type for local storage
 */
#define TIKU_PROCESS_TYPED(name, strname, local_type)                       \
    TIKU_PROCESS_WITH_LOCAL(name, strname, local_type);                     \
    static inline local_type *name##_local(void) {                          \
        return (local_type *)name.local;                                    \
    }

/*---------------------------------------------------------------------------*/
/* PROCESS CONTEXT MACROS                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Open a process thread body (PT_BEGIN on process_pt). */
#define TIKU_PROCESS_BEGIN()        PT_BEGIN(process_pt)
/** @brief Close the body; reaching it ends the process (PT_END). */
#define TIKU_PROCESS_END()          PT_END(process_pt)
/** @brief Give up the CPU; the body resumes on the next event. */
#define TIKU_PROCESS_YIELD()        PT_YIELD(process_pt)
/** @brief Yield; resume past this point on an event once @p cond holds. */
#define TIKU_PROCESS_YIELD_UNTIL(cond) PT_YIELD_UNTIL(process_pt, cond)
/** @brief Wait for the next event; the same as TIKU_PROCESS_YIELD(). */
#define TIKU_PROCESS_WAIT_EVENT()   PT_YIELD(process_pt)
/** @brief Wait for events until @p cond holds; as YIELD_UNTIL. */
#define TIKU_PROCESS_WAIT_EVENT_UNTIL(cond) PT_YIELD_UNTIL(process_pt, cond)
/** @brief End the process from inside its body (PT_EXIT). */
#define TIKU_PROCESS_EXIT()         PT_EXIT(process_pt)
/** @brief The process whose thread is running now. */
#define TIKU_PROCESS_CURRENT()      (tiku_current_process)
/** @brief The process whose thread is running now. */
#define TIKU_THIS()                 (tiku_current_process)

/**
 * @brief Run the code up to TIKU_PROCESS_CONTEXT_END() as process @p p.
 *
 * Sets tiku_current_process to @p p and restores the previous value at the
 * matching TIKU_PROCESS_CONTEXT_END(), so code run on @p p's behalf, such as a
 * timer callback, acts as @p p.
 */
#define TIKU_PROCESS_CONTEXT_BEGIN(p) \
    do { struct tiku_process *_saved = tiku_current_process; \
         tiku_current_process = (p)
/** @brief Close a TIKU_PROCESS_CONTEXT_BEGIN() block. */
#define TIKU_PROCESS_CONTEXT_END(p) \
         tiku_current_process = _saved; } while (0)

/*---------------------------------------------------------------------------*/
/* AUTOSTART                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_AUTOSTART_PROCESSES(...)
 * @brief Register processes for automatic startup
 *
 * Defines the NULL-terminated array of process pointers that
 * tiku_sched_loop() starts before entering the main loop, when
 * TIKU_AUTOSTART_ENABLE is set.
 */
#define TIKU_AUTOSTART_PROCESSES(...)                                       \
    __attribute__((used))                                                  \
    struct tiku_process * const tiku_autostart_processes[] =                \
        {__VA_ARGS__, NULL}

/**
 * @brief Processes to start automatically: TIKU_AUTOSTART_PROCESSES()
 *        defines it, and a weak empty default stands in otherwise.
 */
extern struct tiku_process * const tiku_autostart_processes[];

/**
 * @brief Start every process in a NULL-terminated array with NULL data.
 *
 * Skips this boot's hang culprit (tiku_hang_is_culprit()).
 *
 * @param processes Array of process pointers (last entry must be NULL)
 */
void tiku_autostart_start(struct tiku_process * const processes[]);

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the process scheduler
 *
 * Resets the process list, the event queue and the pid registry.
 *
 * @note Call at startup, before interrupts are enabled and before any
 *       process starts.
 */
void tiku_process_init(void);

/**
 * @brief Start a process
 *
 * Adds the process to the active list and posts an INIT event
 * to it. Does nothing if the process is already running.
 *
 * @param p    Process to start
 * @param data Data passed with the INIT event
 * @note A process stopped by tiku_process_stop() is still on the list, and
 *       starting it links it a second time; tiku_process_resume() is the
 *       call that brings it back.
 */
void tiku_process_start(struct tiku_process *p,
                        tiku_event_data_t data);

/**
 * @brief Exit a process
 *
 * Stops the process, drops its queued events, timers and VFS watches,
 * broadcasts EXITED, and restarts it at once if its restart policy says so.
 * Does nothing when @p p is not running.
 *
 * @param p Process to exit
 */
void tiku_process_exit(struct tiku_process *p);

/*---------------------------------------------------------------------------*/
/* SUPERVISION (per-process restart policy)                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Set a process's restart policy.
 *
 * NEVER (the default) leaves an exited process stopped; ON_FAILURE restarts
 * only after a FAILED exit, ALWAYS after any exit, as a fresh instance under
 * the same pid.  A burst of restarts sets the policy back to NEVER.
 *
 * @note TIKU_SUPERVISOR_MAX_BURST (5) restarts, each within
 *       TIKU_SUPERVISOR_WINDOW_TICKS (5 s) of the last, go ahead; the next
 *       attempt sets the policy to NEVER and leaves the process stopped.
 */
void tiku_process_set_restart(struct tiku_process *p,
                              tiku_restart_policy_t policy);

/**
 * @brief Mark @p p as FAILED so ON_FAILURE / ALWAYS supervision restarts it.
 *
 * A process that ends without this call exits DONE, which ON_FAILURE does not
 * restart.
 *
 * @note Call from inside the process on an unrecoverable error (typically
 *       tiku_process_fail(TIKU_THIS())), then end with TIKU_PROCESS_END().
 */
void tiku_process_fail(struct tiku_process *p);

/** @brief Current restart policy of @p p. */
tiku_restart_policy_t tiku_process_get_restart(const struct tiku_process *p);

/** @brief How @p p last exited (NONE while running). */
tiku_exit_reason_t tiku_process_exit_reason(const struct tiku_process *p);

/** @brief Lifetime count of supervisor restarts of @p p. */
uint16_t tiku_process_restarts(const struct tiku_process *p);

/**
 * @brief Post an event to a process
 *
 * Enqueues an event for delivery; TIKU_PROCESS_BROADCAST as the target
 * delivers it to every running process.  A user-range event is refused once
 * only TIKU_QUEUE_RESERVE slots are free.  Callable from an ISR.
 *
 * @param p    Target process (or TIKU_PROCESS_BROADCAST)
 * @param ev   Event identifier
 * @param data Event data
 * @return 1 if event posted, 0 if the queue is full or the memory-reclaim
 *         gate refuses the event for the target
 */
uint8_t tiku_process_post(struct tiku_process *p, tiku_event_t ev,
                          tiku_event_data_t data);

/**
 * @brief Run the process scheduler
 *
 * Dequeues one event and dispatches it to the target process; an event for
 * an earlier instance of its target is dropped.
 *
 * @return 1 if an event was dequeued, 0 if the queue was empty
 */
uint8_t tiku_process_run(void);

/**
 * @brief Run the scheduler, but never re-enter @p skip.
 *
 * For a long synchronous operation inside @p skip's own dispatch.  Events for
 * @p skip stay queued, except a POLL, which is dropped; a broadcast runs for
 * every other process and is not delivered to @p skip.
 *
 * @param skip Process not to dispatch (typically TIKU_THIS()), or NULL for
 *             plain tiku_process_run()
 * @return 1 if an event was dispatched or discarded, 0 if no eligible work
 *         exists
 */
uint8_t tiku_process_run_except(const struct tiku_process *skip);

/**
 * @brief Test whether run_except(@p skip) has work it can process.
 *
 * True when the queue holds an event for another process, a broadcast, a
 * stale event or a POLL for @p skip.  Callable inside or outside a
 * tiku_atomic_enter()/exit() pair.
 *
 * @param skip Process excluded from dispatch; NULL means any queued event
 * @return 1 if tiku_process_run_except(skip) could make progress, else 0
 */
uint8_t tiku_process_queue_dispatchable_except(const struct tiku_process *skip);

/**
 * @brief Request a process to be polled
 *
 * Queues a TIKU_EVENT_POLL for @p p, merged with one already pending; the
 * request is dropped and counted when the queue is full.
 *
 * @param p Process to poll
 */
void tiku_process_poll(struct tiku_process *p);

/*---------------------------------------------------------------------------*/
/* PROCESS REGISTRY PROTOTYPES                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Register a process in the registry
 *
 * Assigns the first free pid and starts the process unless it is running.
 * A process already in the registry keeps its pid and name and is not
 * started, whatever its state.
 *
 * @param name  Name to give the process (NULL keeps the struct's name)
 * @param p     Process to register (already declared with TIKU_PROCESS)
 * @return pid (0..TIKU_PROCESS_MAX-1) on success, -1 for a NULL @p p or a
 *         full registry
 */
int8_t tiku_process_register(const char *name, struct tiku_process *p);

/**
 * @brief Attach a memory arena to a process for measured accounting.
 *
 * tiku_process_sram_used() and tiku_process_fram_used(), behind ps,
 * /proc/<pid>/sram_used|fram_used and /sys/mem/used, then add the arena's
 * used bytes to the declared figures.  A process has one attached arena.
 *
 * @param p      Process (no-op when NULL)
 * @param arena  const tiku_arena_t* (typed void to keep this header
 *               free of the memory-module include); NULL detaches
 */
void tiku_process_attach_mem_arena(struct tiku_process *p,
                                   const void *arena);

/** @brief SRAM bytes: measured (attached SRAM-tier arena) + declared. */
uint32_t tiku_process_sram_used(const struct tiku_process *p);

/** @brief Bytes outside SRAM: measured (attached non-SRAM-tier arena) +
 *         declared. */
uint32_t tiku_process_fram_used(const struct tiku_process *p);

/**
 * @brief Get a registered process by pid
 *
 * @param pid  Process identifier (0..TIKU_PROCESS_MAX-1)
 * @return Pointer to the process, or NULL if pid is invalid/empty
 */
struct tiku_process *tiku_process_get(int8_t pid);

/**
 * @brief Stop a registered process (set state to STOPPED)
 *
 * It stays on the process list and keeps its pid; events that reach it are
 * dropped until tiku_process_resume().
 *
 * @param pid  Process identifier
 * @return 0 on success, -1 on error
 */
int8_t tiku_process_stop(int8_t pid);

/**
 * @brief Resume a stopped process (set state to READY)
 *
 * Links it again if an exit unlinked it, then posts TIKU_EVENT_CONTINUE.  A
 * process that ended itself runs its body from the top; any other resumes
 * where it yielded.
 *
 * @param pid  Process identifier
 * @return 0 on success, -1 if @p pid names no process or it is not STOPPED
 */
int8_t tiku_process_resume(int8_t pid);

/**
 * @brief Return the number of registered processes, stopped ones included
 *
 * @return Number of non-NULL registry entries
 */
uint8_t tiku_process_count(void);

/**
 * @brief Convert a process state enum to a string
 *
 * @param state  Process state value
 * @return Static string representation (e.g. "running", "stopped"), or
 *         "unknown" for a value out of range
 */
const char *tiku_process_state_str(tiku_process_state_t state);

/*---------------------------------------------------------------------------*/
/* PROCESS CATALOG                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Maximum catalog entries (processes advertised for a later start).
 *
 * Subsystems call tiku_process_catalog_add() at boot to advertise
 * processes that can be started later via the shell "start" command
 * or init system.
 */
#define TIKU_PROCESS_CATALOG_MAX  8

/**
 * @brief Catalog entry: maps a name to a process struct.
 */
typedef struct {
    const char          *name;
    struct tiku_process *proc;
} tiku_process_catalog_entry_t;

/**
 * @brief Add a process to the catalog without starting it.
 *
 * An entry with the same name is updated in place.  The name pointer is
 * stored, not copied.
 *
 * @param name  Human-readable name (e.g. "net", "mqtt")
 * @param proc  Pointer to the process struct
 * @return 0 on success, -1 for a NULL argument or a full catalog
 */
int8_t tiku_process_catalog_add(const char *name,
                                struct tiku_process *proc);

/**
 * @brief Look up a catalog entry by name.
 *
 * @param name  Process name to search for
 * @return Pointer to the process struct, or NULL if not found
 */
struct tiku_process *tiku_process_catalog_find(const char *name);

/**
 * @brief Return the number of catalog entries.
 */
uint8_t tiku_process_catalog_count(void);

/**
 * @brief Get a catalog entry by index.
 *
 * @param idx  Index (0 .. tiku_process_catalog_count()-1)
 * @return Pointer to catalog entry, or NULL if out of range
 */
const tiku_process_catalog_entry_t *tiku_process_catalog_get(uint8_t idx);

/**
 * @brief Find a registered process by name.
 *
 * Searches the process registry, not the catalog.
 *
 * @param name  Process name to search for
 * @return Pointer to the process, or NULL if not found
 */
struct tiku_process *tiku_process_find_by_name(const char *name);

/*---------------------------------------------------------------------------*/
/* QUEUE QUERY PROTOTYPES                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the number of free slots in the event queue; user-range
 *        posts can use all but TIKU_QUEUE_RESERVE of them.
 */
uint8_t tiku_process_queue_space(void);

/** @brief Check if the event queue is full */
uint8_t tiku_process_queue_full(void);

/** @brief Check if the event queue is empty */
uint8_t tiku_process_queue_empty(void);

/** @brief Return the number of pending events in the queue */
uint8_t tiku_process_queue_length(void);

/**
 * @brief Lifetime count of events dropped because the queue was full.
 *
 * Counts every tiku_process_post() or internal poll enqueue that failed for
 * lack of space, including posts refused by the TIKU_QUEUE_RESERVE guard.
 * Wraps at 65535; exported at /proc/queue/dropped.
 */
uint16_t tiku_process_queue_dropped(void);

/**
 * @brief Peek at a queue entry by index (0 = head).
 *
 * @param index  Position from head (0 .. queue_length-1)
 * @param ev     Output: event type (NULL to skip)
 * @param target Output: target process pointer (NULL to skip)
 * @return 0 on success, -1 if index out of range
 */
int8_t tiku_process_queue_peek(uint8_t index, tiku_event_t *ev,
                               struct tiku_process **target);

/** @brief Check if a process is running */
uint8_t tiku_process_is_running(struct tiku_process *p);

/*---------------------------------------------------------------------------*/
/* CHANNEL DECLARATION MACRO                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_CHANNEL_DECLARE(name, type, depth)
 * @brief Declare a channel with type-safe inline accessors
 *
 * Generates static storage, a channel instance, and typed
 * name_init / name_put / name_get helpers.
 *
 * @param name  Identifier prefix (used for buffer, channel, helpers)
 * @param type  Message type (e.g., struct sensor_msg)
 * @param depth Maximum number of buffered messages
 * @note The channel keeps sizeof(type) and @p depth in 8 bits each: a larger
 *       value is truncated without a diagnostic.
 */
#define TIKU_CHANNEL_DECLARE(name, type, depth)                             \
    static type name##_buf[depth];                                          \
    static struct tiku_channel name;                                        \
    static inline void name##_init(void) {                                  \
        tiku_channel_init(&name, name##_buf,                                \
                          sizeof(type), (depth));                           \
    }                                                                       \
    static inline uint8_t name##_put(const type *m) {                       \
        return tiku_channel_put(&name, m);                                  \
    }                                                                       \
    static inline uint8_t name##_get(type *m) {                             \
        return tiku_channel_get(&name, m);                                  \
    }

/*---------------------------------------------------------------------------*/
/* CHANNEL PROTOTYPES                                                        */
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
                       uint8_t msg_size, uint8_t capacity);

/**
 * @brief Put a message into a channel
 *
 * @param ch  Channel to put message into
 * @param msg Pointer to message data
 * @return 1 if stored, 0 if channel is full
 */
uint8_t tiku_channel_put(struct tiku_channel *ch, const void *msg);

/**
 * @brief Get a message from a channel
 *
 * @param ch  Channel to read from
 * @param out Pointer to destination buffer
 * @return 1 if retrieved, 0 if channel is empty
 */
uint8_t tiku_channel_get(struct tiku_channel *ch, void *out);

/**
 * @brief Check if a channel is empty
 *
 * @param ch Channel to check
 * @return 1 if empty, 0 otherwise
 */
uint8_t tiku_channel_is_empty(struct tiku_channel *ch);

/**
 * @brief Return the number of free slots in a channel
 *
 * @param ch Channel to check
 * @return Number of free message slots
 */
uint8_t tiku_channel_free(struct tiku_channel *ch);

/*---------------------------------------------------------------------------*/
/* GLOBAL VARIABLES                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Pointer to the currently executing process; NULL between calls */
extern struct tiku_process *tiku_current_process;

/** @brief Head of the list of started processes, stopped ones included */
extern struct tiku_process *tiku_process_list_head;

#endif /* TIKU_PROCESS_H_ */
