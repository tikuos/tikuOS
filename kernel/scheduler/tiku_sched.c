/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sched.c - Scheduler implementation
 *
 * Central event-driven scheduler. Drains the process event queue,
 * services expired software timers, and enters a low-power idle
 * state when no work is pending.
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

#include "tiku_sched.h"
#include "../timers/tiku_htimer.h"
#include <hal/tiku_cpu.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE VARIABLES                                                         */
/*---------------------------------------------------------------------------*/

/** @brief Scheduler state flag */
static volatile uint8_t sched_state;

/** @brief Platform idle hook (called when no work pending) */
static tiku_sched_idle_hook_t idle_hook;

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the scheduler and all managed subsystems
 *
 * Initialization order matters:
 *   1. Process system (event queue, process list)
 *   2. Hardware timer (platform timer peripheral)
 *   3. Software timers (starts the timer management process)
 */
void tiku_sched_init(void)
{
    sched_state = TIKU_SCHED_RUNNING;
    idle_hook = (tiku_sched_idle_hook_t)0;

    SCHED_PRINTF("Init: process subsystem\n");
    tiku_process_init();
    SCHED_PRINTF("Init: hardware timer\n");
    tiku_htimer_init();
    SCHED_PRINTF("Init: software timers\n");
    tiku_timer_init();
    SCHED_PRINTF("Init complete\n");
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Start a process through the scheduler.
 *
 * Wrapper around tiku_process_start() that adds debug tracing.
 */
void tiku_sched_start(struct tiku_process *p, tiku_event_data_t data)
{
    SCHED_PRINTF("Started: %s\n", p->name);
    tiku_process_start(p, data);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Run one scheduler iteration
 *
 * Dispatches one event from the queue. Timer polling is handled
 * by the clock ISR (which calls tiku_timer_request_poll() on each
 * tick) and by timer_insert() when a timer is set or reset.
 * Polling here would flood the 32-entry event queue with redundant
 * POLL events, causing real events (like TIKU_EVENT_TIMER) to be
 * dropped when the queue is full.
 *
 * @return 1 if an event was dispatched, 0 if idle
 */
uint8_t tiku_sched_run_once(void)
{
    /* Dispatch one event from the queue */
    return tiku_process_run();
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Main scheduler loop
 *
 * The loop follows a standard embedded pattern:
 *   1. Drain all pending events (process + timer)
 *   2. If nothing left, enter idle (low-power)
 *   3. An interrupt wakes the CPU, goto 1
 *
 * The idle hook is called inside an atomic section so that no
 * interrupt is lost between the "is there work?" check and the
 * low-power entry. The platform's low-power instruction should
 * atomically re-enable interrupts (the HAL guarantees this).
 */
void tiku_sched_loop(void)
{
    SCHED_PRINTF("Entering scheduler loop\n");

#if TIKU_AUTOSTART_ENABLE
    tiku_autostart_start(tiku_autostart_processes);
#endif

    /* Enable global interrupts so ISRs (timer tick, UART RX, etc.)
     * can fire.  The scheduler's idle path uses atomic enter/exit
     * which preserves GIE state, so once enabled here it stays on. */
    __enable_interrupt();

    while (sched_state == TIKU_SCHED_RUNNING) {

        /* Drain all pending work */
        while (tiku_sched_run_once()) {
            /* keep dispatching */
        }

        /*
         * No more events — enter idle.
         *
         * The atomic section ensures that if an ISR fires between
         * our check and the idle hook, the resulting event will be
         * processed on the next iteration rather than being missed
         * while we sleep.
         */
        tiku_atomic_enter();

        if (!tiku_sched_has_pending()) {
            if (idle_hook != (tiku_sched_idle_hook_t)0) {
                idle_hook();
            }
        }

        tiku_atomic_exit();
    }
}

/*---------------------------------------------------------------------------*/

/** @brief Signal the scheduler to stop its main loop. */
void tiku_sched_stop(void)
{
    SCHED_PRINTF("Stopped\n");
    sched_state = TIKU_SCHED_STOPPED;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Check if the scheduler has any pending work.
 *
 * Returns non-zero if the process event queue is non-empty or
 * if any software timer is active.
 */
uint8_t tiku_sched_has_pending(void)
{
    /* Work is pending if the process event queue is non-empty
     * or if any software timer is active. */
    if (!tiku_process_queue_empty()) {
        return 1;
    }

    if (tiku_timer_any_pending()) {
        return 1;
    }

    return 0;
}

/*---------------------------------------------------------------------------*/

/** @brief Register a callback invoked when the scheduler is idle. */
void tiku_sched_set_idle_hook(tiku_sched_idle_hook_t hook)
{
    idle_hook = hook;
}

/*---------------------------------------------------------------------------*/

/** @brief Wake the timer management process to check for expired timers. */
void tiku_sched_notify(void)
{
    tiku_timer_request_poll();
}

/*---------------------------------------------------------------------------*/
