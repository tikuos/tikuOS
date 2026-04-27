/*
 * Tiku Operating System v0.02
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

/** @brief Maximum number of processes in the registry */
#define TIKU_PROCESS_MAX        8

/** @brief Post an event to all running processes */
#define TIKU_PROCESS_BROADCAST  NULL

/*---------------------------------------------------------------------------*/
/* SYSTEM EVENTS                                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_EVENT_INIT         0x01
#define TIKU_EVENT_EXIT         0x02
#define TIKU_EVENT_CONTINUE     0x03
#define TIKU_EVENT_POLL         0x04
#define TIKU_EVENT_EXITED       0x05
#define TIKU_EVENT_FORCE_EXIT   0x06
#define TIKU_EVENT_USER         0x10
#define TIKU_EVENT_TIMER        0x88
#define TIKU_EVENT_GPIO         0x89  /**< GPIO pin edge — data carries port/pin */

/** @brief Return code for successful process operations */
#define TIKU_PROCESS_ERR_OK     0

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

typedef uint8_t tiku_event_t;
typedef void *tiku_event_data_t;

/**
 * @brief Process state for observability
 */
typedef enum {
    TIKU_PROCESS_STATE_RUNNING  = 0,
    TIKU_PROCESS_STATE_READY    = 1,
    TIKU_PROCESS_STATE_WAITING  = 2,
    TIKU_PROCESS_STATE_SLEEPING = 3,
    TIKU_PROCESS_STATE_STOPPED  = 4
} tiku_process_state_t;

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
    void *local;                    /**< Per-process local storage pointer.
                                         NULL if no local state.
                                         Points to a user-defined static
                                         struct. Cost: one pointer width. */
    /* --- Observability fields --- */
    tiku_process_state_t state;     /**< Current process state */
    int8_t pid;                     /**< Registry index (-1 = unregistered) */
    uint16_t sram_used;             /**< SRAM bytes allocated for this process */
    uint16_t fram_used;             /**< FRAM bytes allocated for this process */
    tiku_clock_time_t start_time;   /**< Tick count when process started */
    uint16_t wake_count;            /**< Number of times scheduled */
} tiku_process_t;

/*---------------------------------------------------------------------------*/
/* CHANNEL STRUCTURE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Channel control block
 *
 * Provides a typed, fixed-size message queue for inter-process
 * communication. Storage is caller-provided (statically allocated).
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

struct tiku_msg {
    uint8_t type;      /**< Message type — lets receiver know how to cast */
    uint8_t len;       /**< Payload size — for validation */
};

/*---------------------------------------------------------------------------*/
/* PROCESS DECLARATION MACROS                                                */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_PROCESS(name, strname)
 * @brief Declare and define a process (no local storage)
 *
 * For processes that do not need persistent local state across yields.
 * The local pointer is initialized to NULL.
 */
#define TIKU_PROCESS(name, strname)                                         \
    TIKU_PROCESS_THREAD(name, ev, data);                                    \
    struct tiku_process name = {                                             \
        NULL, strname, tiku_process_thread_##name, {0}, 0,                   \
        NULL, /* local = NULL */                                             \
        TIKU_PROCESS_STATE_STOPPED, -1, 0, 0, 0, 0                          \
    }

/**
 * @def TIKU_PROCESS_WITH_LOCAL(name, strname, local_type)
 * @brief Declare and define a process with typed local storage
 *
 * Allocates a static instance of local_type and wires the pointer
 * into the process struct at compile time. No runtime setup needed.
 *
 * The storage is zero-initialized by the C runtime at boot (BSS).
 * If the process is restarted, the storage retains its previous values.
 * Initialize explicitly in the thread body if clean-slate is needed.
 *
 * @param name       Process variable name
 * @param strname    Human-readable name string
 * @param local_type The struct type for local storage
 *
 * Example:
 * @code
 *   struct my_state { uint16_t counter; uint8_t flag; };
 *   TIKU_PROCESS_WITH_LOCAL(my_proc, "my process", struct my_state);
 * @endcode
 */
#define TIKU_PROCESS_WITH_LOCAL(name, strname, local_type)                  \
    TIKU_PROCESS_THREAD(name, ev, data);                                    \
    static local_type tiku_local_##name;                                    \
    struct tiku_process name = {                                             \
        NULL, strname, tiku_process_thread_##name, {0}, 0,                   \
        &tiku_local_##name, /* local wired at compile time */                \
        TIKU_PROCESS_STATE_STOPPED, -1, 0, 0, 0, 0                          \
    }

/**
 * @def TIKU_PROCESS_THREAD(name, ev, data)
 * @brief Declare a process thread function
 */
#define TIKU_PROCESS_THREAD(name, ev, data)                                \
    static PT_THREAD(tiku_process_thread_##name(                           \
        struct pt *process_pt, tiku_event_t ev,                            \
        tiku_event_data_t data))

/**
 * @def TIKU_LOCAL(type)
 * @brief Access the current process's local storage with a typed cast
 *
 * Returns a pointer to the local storage of the currently running
 * process, cast to the specified type. Must only be called from
 * within a process thread body.
 *
 * This is the quick-and-easy accessor. The compiler trusts you to
 * pass the correct type. For stronger safety, use TIKU_LOCAL_OF()
 * or the per-process typed accessor generated by TIKU_PROCESS_TYPED().
 *
 * Place this ABOVE TIKU_PROCESS_BEGIN() so it runs on every re-entry
 * of the protothread function (the pointer variable is a stack local
 * that doesn't survive across yields).
 *
 * @param type The struct type of the local storage
 * @return Pointer to the typed local storage
 *
 * Example:
 * @code
 *   TIKU_PROCESS_THREAD(my_proc, ev, data) {
 *       struct my_state *s = TIKU_LOCAL(struct my_state);
 *       TIKU_PROCESS_BEGIN();
 *       s->counter = 0;
 *       // ...
 *   }
 * @endcode
 */
#define TIKU_LOCAL(type) ((type *)TIKU_THIS()->local)

/**
 * @def TIKU_PROCESS_TYPED(name, strname, local_type)
 * @brief Declare a process with a per-process type-safe accessor (NEW)
 *
 * Combines TIKU_PROCESS_WITH_LOCAL with a generated inline accessor
 * function: name_local(), which returns a correctly typed pointer.
 *
 * This is the safest option — no casts in user code, impossible
 * to accidentally pass the wrong type.
 *
 * @param name       Process variable name
 * @param strname    Human-readable name string
 * @param local_type The struct type for local storage
 *
 * Example:
 * @code
 *   struct sensor_state { uint16_t reading; uint8_t count; };
 *   TIKU_PROCESS_TYPED(sensor_proc, "sensor", struct sensor_state);
 *
 *   TIKU_PROCESS_THREAD(sensor_proc, ev, data) {
 *       struct sensor_state *s = sensor_proc_local();  // fully type-safe
 *       // ...
 *   }
 * @endcode
 */
#define TIKU_PROCESS_TYPED(name, strname, local_type)                       \
    TIKU_PROCESS_WITH_LOCAL(name, strname, local_type);                     \
    static inline local_type *name##_local(void) {                          \
        return (local_type *)name.local;                                    \
    }

/*---------------------------------------------------------------------------*/
/* PROCESS CONTEXT MACROS                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_PROCESS_BEGIN()        PT_BEGIN(process_pt)
#define TIKU_PROCESS_END()          PT_END(process_pt)
#define TIKU_PROCESS_YIELD()        PT_YIELD(process_pt)
#define TIKU_PROCESS_YIELD_UNTIL(cond) PT_YIELD_UNTIL(process_pt, cond)
#define TIKU_PROCESS_WAIT_EVENT()   PT_YIELD(process_pt)
#define TIKU_PROCESS_WAIT_EVENT_UNTIL(cond) PT_YIELD_UNTIL(process_pt, cond)
#define TIKU_PROCESS_EXIT()         PT_EXIT(process_pt)
#define TIKU_PROCESS_CURRENT()      (tiku_current_process)
#define TIKU_THIS()                 (tiku_current_process)

#define TIKU_PROCESS_CONTEXT_BEGIN(p) \
    do { struct tiku_process *_saved = tiku_current_process; \
         tiku_current_process = (p)
#define TIKU_PROCESS_CONTEXT_END(p) \
         tiku_current_process = _saved; } while (0)

/*---------------------------------------------------------------------------*/
/* AUTOSTART                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_AUTOSTART_PROCESSES(...)
 * @brief Register processes for automatic startup
 *
 * Defines a NULL-terminated array of process pointers that the
 * scheduler will start automatically at the beginning of the
 * main loop (inside tiku_sched_loop).
 *
 * Example:
 * @code
 *   TIKU_AUTOSTART_PROCESSES(&proc_a, &proc_b);
 * @endcode
 */
#define TIKU_AUTOSTART_PROCESSES(...)                                       \
    __attribute__((used))                                                  \
    struct tiku_process * const tiku_autostart_processes[] =                \
        {__VA_ARGS__, NULL}

/** @brief Array of processes to start automatically (defined by user) */
extern struct tiku_process * const tiku_autostart_processes[];

/**
 * @brief Start all processes in a NULL-terminated array
 * @param processes Array of process pointers (last entry must be NULL)
 */
void tiku_autostart_start(struct tiku_process * const processes[]);

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the process scheduler
 *
 * Resets the process list and event queue. Must be called once
 * at system startup before any processes are started.
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
 */
void tiku_process_start(struct tiku_process *p,
                        tiku_event_data_t data);

/**
 * @brief Exit a process
 *
 * Marks the process as stopped and removes it from the active list.
 *
 * @param p Process to exit
 */
void tiku_process_exit(struct tiku_process *p);

/**
 * @brief Post an event to a process
 *
 * Enqueues an event for delivery. Use TIKU_PROCESS_BROADCAST as the
 * target to deliver the event to all running processes.
 *
 * @param p    Target process (or TIKU_PROCESS_BROADCAST)
 * @param ev   Event identifier
 * @param data Event data
 * @return 1 if event posted, 0 if queue full
 */
uint8_t tiku_process_post(struct tiku_process *p, tiku_event_t ev,
                          tiku_event_data_t data);

/**
 * @brief Run the process scheduler
 *
 * Dequeues one event and dispatches it to the target process.
 * Returns 0 when the event queue is empty (safe to enter low-power mode).
 *
 * @return 1 if an event was processed, 0 if idle
 */
uint8_t tiku_process_run(void);

/**
 * @brief Request a process to be polled
 *
 * Marks a process for polling. The process will receive a
 * TIKU_EVENT_POLL event on the next scheduler run.
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
 * Assigns a pid, starts the process, and records its start_time.
 * The process is added to both the registry and the active linked list.
 *
 * @param name  Human-readable name (must match process struct name)
 * @param p     Process to register (already declared with TIKU_PROCESS)
 * @return pid (0..TIKU_PROCESS_MAX-1) on success, -1 if registry full
 */
int8_t tiku_process_register(const char *name, struct tiku_process *p);

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
 * The scheduler will skip this process until it is resumed.
 *
 * @param pid  Process identifier
 * @return 0 on success, -1 on error
 */
int8_t tiku_process_stop(int8_t pid);

/**
 * @brief Resume a stopped process (set state to READY)
 *
 * @param pid  Process identifier
 * @return 0 on success, -1 on error
 */
int8_t tiku_process_resume(int8_t pid);

/**
 * @brief Return the number of registered (active) processes
 *
 * @return Number of non-NULL registry entries
 */
uint8_t tiku_process_count(void);

/**
 * @brief Convert a process state enum to a string
 *
 * @param state  Process state value
 * @return Static string representation (e.g. "running", "stopped")
 */
const char *tiku_process_state_str(tiku_process_state_t state);

/*---------------------------------------------------------------------------*/
/* PROCESS CATALOG                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Maximum catalog entries (available-but-not-yet-started processes).
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
 * @brief Add a process to the catalog (does NOT start it).
 *
 * @param name  Human-readable name (e.g. "net", "mqtt")
 * @param proc  Pointer to the process struct
 * @return 0 on success, -1 if catalog is full
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
 * @brief Find a registered (active) process by name.
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

/** @brief Return the number of free slots in the event queue */
uint8_t tiku_process_queue_space(void);

/** @brief Check if the event queue is full */
uint8_t tiku_process_queue_full(void);

/** @brief Check if the event queue is empty */
uint8_t tiku_process_queue_empty(void);

/** @brief Return the number of pending events in the queue */
uint8_t tiku_process_queue_length(void);

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
 * init / put / get helpers.  The compiler will reject pointer
 * type mismatches at call sites.
 *
 * @param name  Identifier prefix (used for buffer, channel, helpers)
 * @param type  Message type (e.g., struct sensor_msg)
 * @param depth Maximum number of buffered messages
 *
 * Example:
 * @code
 *   TIKU_CHANNEL_DECLARE(sensor_ch, struct sensor_msg, 4);
 *
 *   sensor_ch_init();
 *   sensor_ch_put(&msg);
 *   sensor_ch_get(&msg);
 * @endcode
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

/** @brief Pointer to the currently executing process */
extern struct tiku_process *tiku_current_process;

/** @brief Head of the active process linked list */
extern struct tiku_process *tiku_process_list_head;

#endif /* TIKU_PROCESS_H_ */
