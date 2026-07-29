/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sched.h - scheduler interface.
 *
 * Coordinates process dispatch, timer expiration and low-power idle.  The main
 * loop lives here, so main.c only calls tiku_sched_init() and tiku_sched_loop().
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
 * @brief Optional hook called when the scheduler has no pending work.
 *
 * The platform may register a function called whenever there are no events and
 * no timers due, typically to enter a low-power mode.  It should return; the
 * scheduler re-checks for work afterwards.
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
 * @brief Enter the main scheduler loop (never returns).
 *
 * Dispatches events and checks timers, calling the idle hook when nothing is
 * pending so the platform can drop into a low-power mode until an interrupt.
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
 * @brief Check if there is pending work.
 *
 * Non-zero when the event queue is non-empty or a software timer is due.  A
 * timer armed for a future deadline is not pending work: the tick ISR wakes the
 * CPU when it arrives.
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
 * @brief Declare whether the current idle mode is woken by the tick.
 *
 * Non-zero (the default) lets the scheduler idle while timers are armed, since
 * the tick wakes the CPU.  Zero for a mode whose wake sources exclude the tick,
 * where sleeping would miss the deadline forever.
 *
 * @param wakes Non-zero if the tick wakes the registered idle mode
 */
void tiku_sched_set_idle_tick_wakes(uint8_t wakes);

/**
 * @brief Return the number of times the scheduler entered idle.
 */
uint16_t tiku_sched_idle_count(void);

/**
 * @brief Notify the scheduler from ISR context
 *
 * Call this from any ISR that generates work (e.g., clock tick ISR).
 * It requests the timer process to poll for expired timers.
 */
void tiku_sched_notify(void);

#endif /* TIKU_SCHED_H_ */
