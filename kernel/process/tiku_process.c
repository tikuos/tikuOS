/*
 * Tiku Operating System
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
#include <arch/msp430/tiku_compiler.h>
#include <hal/tiku_cpu.h>
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE TYPES                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Single entry in the event queue */
struct event_item {
    tiku_event_t ev;
    tiku_event_data_t data;
    struct tiku_process *p;
};

/*---------------------------------------------------------------------------*/
/* PRIVATE VARIABLES                                                         */
/*---------------------------------------------------------------------------*/

/* Event queue — volatile because ISRs may call tiku_process_post() */
static struct event_item queue[TIKU_QUEUE_SIZE];
static volatile uint8_t q_head = 0;
static volatile uint8_t q_len = 0;

/*---------------------------------------------------------------------------*/
/* PUBLIC VARIABLES                                                          */
/*---------------------------------------------------------------------------*/

struct tiku_process *tiku_list_head = NULL;
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
    tiku_list_head = NULL;
    tiku_current_process = NULL;
    q_head = 0;
    q_len = 0;
    PROCESS_PRINTF("Init complete\n");
}

/**
 * @brief Start a process
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

    p->next = tiku_list_head;
    tiku_list_head = p;
    p->is_running = 1;

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
 * @param p Process to exit
 */
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

    if (tiku_list_head == p) {
        tiku_list_head = p->next;
    } else {
        for (q = tiku_list_head; q != NULL; q = q->next) {
            if (q->next == p) {
                q->next = p->next;
                break;
            }
        }
    }

    tiku_atomic_exit();
}

/**
 * @brief Post an event to a process
 *
 * Safe to call from interrupt context.
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

    tiku_atomic_enter();

    if (q_len < TIKU_QUEUE_SIZE) {
        uint8_t idx = (q_head + q_len) % TIKU_QUEUE_SIZE;
        queue[idx].ev = ev;
        queue[idx].data = data;
        queue[idx].p = p;
        q_len++;
        ret = 1;
    }

    tiku_atomic_exit();

    return ret;
}

/**
 * @brief Run the process scheduler
 *
 * Dequeues one event and dispatches it. Returns 0 when idle.
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
        struct tiku_process *p;
        for (p = tiku_list_head; p != NULL; p = p->next) {
            call_process(p, ev, data);
        }
    } else {
        call_process(receiver, ev, data);
    }

    return 1;
}

/**
 * @brief Request a process to be polled
 *
 * Posts a poll event to the target process. If the queue is full, the
 * request is dropped to preserve ISR safety.
 *
 * @param p Process to poll
 */
void tiku_process_poll(struct tiku_process *p)
{
    (void)tiku_process_post(p, TIKU_EVENT_POLL, NULL);
}

/*---------------------------------------------------------------------------*/
/* PRIVATE FUNCTIONS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Dispatch an event to a single process
 *
 * Runs the process thread and handles automatic exit when the
 * thread returns PT_EXITED, PT_ENDED, or receives TIKU_EVENT_EXIT.
 *
 * @param p    Target process
 * @param ev   Event to deliver
 * @param data Associated event data
 */
static void call_process(struct tiku_process *p, tiku_event_t ev,
                         tiku_event_data_t data)
{
    int ret;

    if (p->is_running && p->thread) {
        tiku_current_process = p;
        ret = p->thread(&p->pt, ev, data);
        if (ret == PT_EXITED || ret == PT_ENDED ||
            ev == TIKU_EVENT_EXIT) {
            tiku_process_exit(p);
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
