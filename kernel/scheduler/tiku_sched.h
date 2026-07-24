/*
 * Tiku Operating System v0.06
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
 * timer is DUE (expired but not yet dispatched). A timer armed for
 * a future deadline is not pending work: the tick ISR wakes the CPU
 * when it comes due. Useful for deciding whether to enter low-power
 * mode.
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
 * @brief Declare whether the current idle mode is woken by the tick
 *
 * When non-zero (the default), the scheduler will idle even while
 * software timers are armed: the tick interrupt wakes the CPU and
 * the due timer is dispatched on the next pass. Set to zero when
 * registering an idle mode whose wake sources do not include the
 * system tick (e.g. MSP430 LPM4) — the scheduler then refuses to
 * idle while any timer is armed, since sleeping would miss the
 * deadline forever. Pair with tiku_cpu_idle_mode_wakes_on_tick().
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
