/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer.h - Unified software timer interface
 *
 * Each timer can operate in callback mode (calls a function directly when
 * expired) or event mode (posts TIKU_EVENT_TIMER to a process). Both modes
 * share the same structure, linked list, and management process.
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

#ifndef TIKU_TIMER_H_
#define TIKU_TIMER_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "../process/tiku_process.h"
#include "tiku_clock.h"

/*---------------------------------------------------------------------------*/
/* TIMER MODES                                                               */
/*---------------------------------------------------------------------------*/

/**
 * Timer operation modes.
 * Determines what happens when the timer expires.
 */
enum tiku_timer_mode {
  TIKU_TIMER_MODE_EVENT = 0,    /**< Post event to owning process */
  TIKU_TIMER_MODE_CALLBACK = 1, /**< Call function directly */
};

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @typedef tiku_timer_callback_t
 * @brief Callback function type
 * @param ptr User-defined pointer
 */
typedef void (*tiku_timer_callback_t)(void *ptr);

/**
 * @struct tiku_timer
 * @brief Unified software timer structure
 *
 * In EVENT mode, the process field determines where the expiration
 * event goes. In CALLBACK mode, the func/ptr fields determine
 * what gets called.
 *
 * Memory cost per timer: ~20-24 bytes
 */
struct tiku_timer {
  struct tiku_timer *next; /**< Linked list pointer (internal) */

  /* Timing state */
  tiku_clock_time_t start;    /**< When the timer was set */
  tiku_clock_time_t interval; /**< Duration in clock ticks */

  /* Dispatch info */
  uint8_t mode;   /**< TIKU_TIMER_MODE_EVENT or _CALLBACK */
  uint8_t active; /**< Non-zero if timer is in the active list */

  struct tiku_process *p;     /**< Process: event target (EVENT) or
                                   callback context (CALLBACK) */
  tiku_timer_callback_t func; /**< Callback function (CALLBACK mode) */
  void *ptr;                  /**< User data for callback */
};

/*---------------------------------------------------------------------------*/
/* CORE API                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the timer subsystem
 *
 * Starts the internal timer management process.
 * Call once during system init, after the process system is up.
 */
void tiku_timer_init(void);

/**
 * @brief Set a callback timer
 * @param t     Timer structure (caller-owned, must persist)
 * @param ticks Interval in clock ticks
 * @param func  Function to call on expiration
 * @param ptr   User data passed to func
 *
 * If the timer is already active, it is stopped and re-set.
 * Callback runs in the context of the process that called this function.
 *
 * Example:
 * @code
 *   static struct tiku_timer my_timer;
 *   tiku_timer_set_callback(&my_timer, TIKU_CLOCK_SECOND * 2,
 *                           on_timeout, NULL);
 * @endcode
 */
void tiku_timer_set_callback(struct tiku_timer *t, tiku_clock_time_t ticks,
                             tiku_timer_callback_t func, void *ptr);

/**
 * @brief Set an event timer
 * @param t     Timer structure (caller-owned, must persist)
 * @param ticks Interval in clock ticks
 *
 * Posts TIKU_EVENT_TIMER to the calling process when
 * the timer expires. The event data pointer will be `t`.
 *
 * Example:
 * @code
 *   static struct tiku_timer my_timer;
 *   tiku_timer_set_event(&my_timer, TIKU_CLOCK_SECOND);
 *   TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
 * @endcode
 */
void tiku_timer_set_event(struct tiku_timer *t, tiku_clock_time_t ticks);

/**
 * @brief Reset timer for drift-free periodic operation
 * @param t Timer structure
 *
 * Re-adds the timer with start = old_start + interval, keeping
 * the same mode/callback/process. This avoids cumulative drift.
 *
 * Safe to call from within a callback.
 */
void tiku_timer_reset(struct tiku_timer *t);

/**
 * @brief Restart timer from current time
 * @param t Timer structure
 *
 * Like reset but anchored to now. Use when you don't care about
 * drift (e.g., retriggering a timeout on activity).
 */
void tiku_timer_restart(struct tiku_timer *t);

/**
 * @brief Stop a timer
 * @param t Timer structure
 *
 * Removes the timer from the active list. Safe to call even if
 * the timer is not active (no-op in that case).
 */
void tiku_timer_stop(struct tiku_timer *t);

/**
 * @brief Check if a timer has expired
 * @param t Timer structure
 * @return Non-zero if expired (not in active list), zero if pending
 */
int tiku_timer_expired(struct tiku_timer *t);

/**
 * @brief Get remaining time until expiration
 * @param t Timer structure
 * @return Ticks remaining, 0 if expired
 */
tiku_clock_time_t tiku_timer_remaining(struct tiku_timer *t);

/**
 * @brief Get the absolute expiration time
 * @param t Timer structure
 * @return start + interval (the tick at which this timer fires)
 */
tiku_clock_time_t tiku_timer_expiration_time(struct tiku_timer *t);

/*---------------------------------------------------------------------------*/
/* SYSTEM QUERIES                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Check if any software timers are pending
 * @return Non-zero if at least one timer is active
 */
int tiku_timer_any_pending(void);

/**
 * @brief Get next expiration time across all timers
 * @return Nearest expiration time, or 0 if none pending
 *
 * Useful for the scheduler to know how long it can sleep.
 */
tiku_clock_time_t tiku_timer_next_expiration(void);

/**
 * @brief Request the timer process to poll (called from clock ISR)
 */
void tiku_timer_request_poll(void);

/*---------------------------------------------------------------------------*/
/* SYSTEM PROCESS                                                            */
/*---------------------------------------------------------------------------*/

/** The single timer management process */
extern struct tiku_process tiku_timer_process;

/*---------------------------------------------------------------------------*/
/* CONVENIENCE MACROS                                                        */
/*---------------------------------------------------------------------------*/

/** One second in timer ticks */
#define TIKU_TIMER_SECOND TIKU_CLOCK_SECOND

/** One minute in timer ticks */
#define TIKU_TIMER_MINUTE (TIKU_CLOCK_SECOND * 60UL)

#endif /* TIKU_TIMER_H_ */
