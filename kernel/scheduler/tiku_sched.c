/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
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

/** @brief Number of times the scheduler entered idle */
static volatile uint16_t idle_count;

/**
 * @brief Whether the registered idle mode is woken by the system tick.
 *
 * Gates idling while software timers are ARMED (not yet due): in a
 * tick-woken mode (MSP430 LPM0-3, any WFI mode on the Cortex-M
 * parts) the tick ISR wakes the CPU and posts the timer poll, so
 * sleeping with armed timers is safe -- and is the whole point of
 * deadline-aware idle.  In a mode whose wake set does not include
 * the tick (MSP430 LPM4: all clocks off) an armed timer would sleep
 * past its deadline forever, so the scheduler falls back to the old
 * conservative behavior and stays awake until the timer fires.
 *
 * Defaults to 1: a NULL hook makes the gate moot, and the only
 * boot-time hook (the RP2350 USB-CDC poll) is not a sleep at all.
 * The `sleep` command updates it per mode via
 * tiku_cpu_idle_mode_wakes_on_tick().
 */
static uint8_t idle_tick_wakes = 1;

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
    tiku_cpu_irq_enable();

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
         *
         * An ARMED (not yet due) timer does not block idle when the
         * registered idle mode is tick-woken: the tick ISR wakes the
         * CPU, posts the timer poll, and the next loop pass
         * dispatches it (the MSP430 tick ISR clears the LPM bits on
         * exit; the Cortex-M modes are plain WFI).  Only when the
         * idle mode's wake set excludes the tick (idle_tick_wakes
         * == 0, e.g. MSP430 LPM4) do armed timers keep the CPU
         * awake — sleeping would miss the deadline forever.
         */
        tiku_atomic_enter();

        if (!tiku_sched_has_pending() &&
            (idle_tick_wakes || !tiku_timer_any_pending())) {
            idle_count++;
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
 * @brief Check if the scheduler has dispatchable work RIGHT NOW.
 *
 * Returns non-zero if the process event queue is non-empty or any
 * software timer is due (expired but not yet dispatched).  A timer
 * that is merely ARMED for a future deadline is not pending work —
 * the tick ISR will wake the CPU and post its poll when the
 * deadline arrives.  (Before deadline-aware idle this predicate
 * treated any armed timer as pending, which made the scheduler
 * busy-spin between ticks for as long as any timer existed.)
 */
uint8_t tiku_sched_has_pending(void)
{
    /* Work is pending if the process event queue is non-empty
     * or if any software timer is DUE (not merely armed). */
    if (!tiku_process_queue_empty()) {
        return 1;
    }

    if (tiku_timer_work_pending()) {
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

/** @brief Declare whether the registered idle mode wakes on the tick. */
void tiku_sched_set_idle_tick_wakes(uint8_t wakes)
{
    idle_tick_wakes = wakes ? 1u : 0u;
}

/*---------------------------------------------------------------------------*/

/** @brief Return the number of idle entries since boot. */
uint16_t tiku_sched_idle_count(void)
{
    return idle_count;
}

/** @brief Wake the timer management process to check for expired timers. */
void tiku_sched_notify(void)
{
    tiku_timer_request_poll();
}

/*---------------------------------------------------------------------------*/
