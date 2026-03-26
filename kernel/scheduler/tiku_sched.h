/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sched.h - Scheduler interface
 *
 * Central scheduler for the Tiku Operating System. Coordinates process
 * dispatch, timer expiration, and low-power idle. The main loop lives
 * here so that main.c only needs to call tiku_sched_init() and
 * tiku_sched_loop().
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

#ifndef TIKU_SCHED_H_
#define TIKU_SCHED_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "../process/tiku_process.h"
#include "../timers/tiku_timer.h"

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Scheduler is running normally */
#define TIKU_SCHED_RUNNING      0

/** @brief Scheduler has been asked to stop */
#define TIKU_SCHED_STOPPED      1

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @typedef tiku_sched_idle_hook_t
 * @brief Optional hook called when the scheduler has no pending work
 *
 * The platform can register a function that is called each time the
 * scheduler finds no events to process and no timers due. Typical use
 * is to enter a low-power mode. The hook should return; the scheduler
 * will re-check for work after it returns.
 */
typedef void (*tiku_sched_idle_hook_t)(void);

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the scheduler and all managed subsystems
 *
 * Initializes the process system, software timer subsystem, and
 * hardware timer. Must be called once at startup after clock init.
 */
void tiku_sched_init(void);

/**
 * @brief Start a process through the scheduler
 *
 * Convenience wrapper around tiku_process_start().
 *
 * @param p    Process to start
 * @param data Data passed with the INIT event
 */
void tiku_sched_start(struct tiku_process *p, tiku_event_data_t data);

/**
 * @brief Run one scheduler iteration
 *
 * Checks for expired timers, then dispatches one event from the
 * process event queue. Returns whether any work was done.
 *
 * @return 1 if an event was dispatched, 0 if idle
 */
uint8_t tiku_sched_run_once(void);

/**
 * @brief Enter the main scheduler loop (never returns)
 *
 * Repeatedly dispatches events and checks timers. When no work is
 * pending, calls the idle hook (if set) to allow the platform to
 * enter a low-power mode. An interrupt will wake the CPU and the
 * loop continues.
 */
void tiku_sched_loop(void);

/**
 * @brief Stop the scheduler loop
 *
 * Sets a flag that causes tiku_sched_loop() to return on its next
 * iteration. Primarily useful for test harnesses.
 */
void tiku_sched_stop(void);

/**
 * @brief Check if there is pending work
 *
 * Returns non-zero if the event queue is non-empty or any software
 * timer is due. Useful for deciding whether to enter low-power mode.
 *
 * @return Non-zero if work is pending
 */
uint8_t tiku_sched_has_pending(void);

/**
 * @brief Register an idle hook
 *
 * The idle hook is called whenever the scheduler has drained the
 * event queue and no timers are immediately due. Typical usage is
 * to enter a platform-specific low-power mode.
 *
 * @param hook Function to call when idle (NULL to clear)
 */
void tiku_sched_set_idle_hook(tiku_sched_idle_hook_t hook);

/**
 * @brief Notify the scheduler from ISR context
 *
 * Call this from any ISR that generates work (e.g., clock tick ISR).
 * It requests the timer process to poll for expired timers.
 */
void tiku_sched_notify(void);

#endif /* TIKU_SCHED_H_ */
