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
#include <string.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE TYPES                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Single entry in the event queue */
struct event_item {
    tiku_event_data_t data;
    struct tiku_process *p;
    tiku_event_t ev;
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

struct tiku_process *tiku_process_list_head = NULL;
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
    tiku_process_list_head = NULL;
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

    p->next = tiku_process_list_head;
    tiku_process_list_head = p;
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
    tiku_process_post(TIKU_PROCESS_BROADCAST,
                      TIKU_EVENT_EXITED,
                      (tiku_event_data_t)p);
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
 * thread returns PT_EXITED, PT_ENDED, or receives TIKU_EVENT_FORCE_EXIT.
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
        ret = p->thread(&p->pt, ev, data);
        tiku_current_process = NULL;
        if (ret == PT_EXITED || ret == PT_ENDED ||
            ev == TIKU_EVENT_FORCE_EXIT) {
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