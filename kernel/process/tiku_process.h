/*
 * Tiku Operating System
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
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Event queue size (power of 2 for fast modulo) */
#define TIKU_QUEUE_SIZE         32

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
#define TIKU_EVENT_USER         0x10
#define TIKU_EVENT_TIMER        0x88

/** @brief Return code for successful process operations */
#define TIKU_PROCESS_ERR_OK     0

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

typedef uint8_t tiku_event_t;
typedef void *tiku_event_data_t;

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
} tiku_process_t;

/*---------------------------------------------------------------------------*/
/* PROCESS DECLARATION MACROS                                                */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_PROCESS(name, strname)
 * @brief Declare and define a process
 */
#define TIKU_PROCESS(name, strname)                                        \
    TIKU_PROCESS_THREAD(name, ev, data);                                   \
    struct tiku_process name = {                                            \
        NULL, strname, tiku_process_thread_##name, {0}, 0                  \
    }

/**
 * @def TIKU_PROCESS_THREAD(name, ev, data)
 * @brief Declare a process thread function
 */
#define TIKU_PROCESS_THREAD(name, ev, data)                                \
    static PT_THREAD(tiku_process_thread_##name(                           \
        struct pt *process_pt, tiku_event_t ev,                            \
        tiku_event_data_t data))

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
/* GLOBAL VARIABLES                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Pointer to the currently executing process */
extern struct tiku_process *tiku_current_process;

#endif /* TIKU_PROCESS_H_ */
