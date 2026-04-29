/*
 * Tiku Operating System v0.03
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
 * @brief Check whether a timer is not currently in the active list.
 * @param t Timer structure
 * @return Non-zero if the timer is inactive, zero if it is pending.
 *
 * A timer is "inactive" in two distinct cases:
 *   1. it has never been set (the struct is zero-initialised), or
 *   2. it was set, fired, and has been dispatched.
 *
 * This call cannot tell those two cases apart — callers that need to
 * distinguish "fired since I set it" from "never set" must track that
 * themselves (e.g. set a sentinel before tiku_timer_set_*). The
 * common pattern of "set then PT_WAIT_UNTIL(tiku_timer_expired(t))"
 * is unaffected because the caller knows the timer was just set.
 */
int tiku_timer_expired(struct tiku_timer *t);

/**
 * @brief Get remaining time until expiration.
 * @param t Timer structure
 * @return Ticks remaining if the timer is active and pending; 0
 *         otherwise.
 *
 * A return of 0 means the timer is not currently pending, which
 * covers both "already fired" and "never set" — see
 * tiku_timer_expired() for the same caveat.
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
 * @brief Return the number of active software timers.
 */
uint8_t tiku_timer_count(void);

/**
 * @brief Return the total number of timer expirations since boot.
 */
uint16_t tiku_timer_fired(void);

/**
 * @brief Get an active timer by index (0 = first in list).
 * @return Pointer to timer, or NULL if index out of range
 */
struct tiku_timer *tiku_timer_get(uint8_t idx);

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

/*---------------------------------------------------------------------------*/
/* TIMEOUT HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/*
 * Convenience macros that wrap a one-shot tiku_timer_set_event() and
 * the matching PT_WAIT_UNTIL / PT_YIELD_UNTIL into a single call,
 * removing the repetitive timer-set-then-wait-then-stop boilerplate
 * found in many process bodies.
 *
 * Caller-side contract:
 *
 *   - The caller owns a `struct tiku_timer` (typically a static
 *     variable in the process file) and passes its address to the
 *     macro.  One timer per call site is the cleanest mapping.
 *
 *   - After the macro returns, re-evaluate the same condition at the
 *     call site to distinguish "condition met" from "timed out".
 *     The macro stops the timer on exit so a stray TIKU_EVENT_TIMER
 *     cannot be posted to the process after the wait completes.
 *
 *   - The condition expression is re-evaluated whenever the process
 *     is re-scheduled, exactly the same as plain PT_WAIT_UNTIL.
 *
 * Example -- replaces the four-line set/wait/stop dance:
 * @code
 *   static struct tiku_timer t;
 *
 *   PT_WAIT_UNTIL_TIMEOUT(pt, &t, sensor_ready(),
 *                         TIKU_CLOCK_SECOND * 2);
 *   if (sensor_ready()) {
 *       // success path
 *   } else {
 *       // timeout path
 *   }
 * @endcode
 */

/**
 * @def PT_WAIT_UNTIL_TIMEOUT(pt, timer, cond, ticks)
 * @brief Block until @p cond is true or @p ticks have elapsed
 *
 * Sets a one-shot event timer for @p ticks clock ticks, blocks the
 * protothread (returning PT_WAITING -- the process appears as
 * "waiting" in /proc and ps) until @p cond evaluates true or the
 * timer fires, then stops the timer.
 *
 * @param pt    Pointer to the protothread control block
 * @param timer Pointer to a caller-owned struct tiku_timer
 * @param cond  Boolean expression re-evaluated on each schedule
 * @param ticks Timeout duration in clock ticks (use TIKU_CLOCK_SECOND
 *              and friends for readability)
 */
#define PT_WAIT_UNTIL_TIMEOUT(pt, timer, cond, ticks)                  \
  do {                                                                  \
    tiku_timer_set_event((timer), (ticks));                            \
    PT_WAIT_UNTIL((pt), (cond) || tiku_timer_expired(timer));          \
    tiku_timer_stop(timer);                                            \
  } while (0)

/**
 * @def PT_YIELD_UNTIL_TIMEOUT(pt, timer, cond, ticks)
 * @brief Yield until @p cond is true or @p ticks have elapsed
 *
 * Identical to PT_WAIT_UNTIL_TIMEOUT but uses PT_YIELD_UNTIL semantics
 * (returning PT_YIELDED -- the process appears as "ready" in /proc
 * and ps).  Use this when the wait is part of a cooperative polling
 * loop that the scheduler should consider immediately runnable
 * between checks rather than parked.
 *
 * @param pt    Pointer to the protothread control block
 * @param timer Pointer to a caller-owned struct tiku_timer
 * @param cond  Boolean expression re-evaluated on each schedule
 * @param ticks Timeout duration in clock ticks
 */
#define PT_YIELD_UNTIL_TIMEOUT(pt, timer, cond, ticks)                 \
  do {                                                                  \
    tiku_timer_set_event((timer), (ticks));                            \
    PT_YIELD_UNTIL((pt), (cond) || tiku_timer_expired(timer));         \
    tiku_timer_stop(timer);                                            \
  } while (0)

#if TIKU_LC_PERSISTENT

/**
 * @def PT_WAIT_UNTIL_TIMEOUT_PERSISTENT(pt, timer, cond, ticks)
 * @brief Persistent variant of PT_WAIT_UNTIL_TIMEOUT
 *
 * Behaves like PT_WAIT_UNTIL_TIMEOUT but the underlying wait is
 * PT_WAIT_UNTIL_PERSISTENT, so the continuation point is checkpointed
 * to NVM via the persist store.
 *
 * Post-reboot semantics: the struct tiku_timer lives in RAM and is
 * lost across power cycles, so when the protothread resumes from the
 * NVM checkpoint inside the wait, tiku_timer_expired() reports the
 * (zeroed) timer as already expired and the macro falls through
 * immediately as if the wait had timed out.  The caller code that
 * follows the macro should re-check @p cond and treat a false result
 * as "the wait was interrupted by power loss -- retry".
 *
 * Available only when TIKU_LC_PERSISTENT is defined.
 *
 * @param pt    Pointer to the protothread control block
 * @param timer Pointer to a caller-owned struct tiku_timer
 * @param cond  Boolean expression re-evaluated on each schedule
 * @param ticks Timeout duration in clock ticks
 */
#define PT_WAIT_UNTIL_TIMEOUT_PERSISTENT(pt, timer, cond, ticks)       \
  do {                                                                  \
    tiku_timer_set_event((timer), (ticks));                            \
    PT_WAIT_UNTIL_PERSISTENT((pt),                                     \
                             (cond) || tiku_timer_expired(timer));     \
    tiku_timer_stop(timer);                                            \
  } while (0)

/**
 * @def PT_YIELD_UNTIL_TIMEOUT_PERSISTENT(pt, timer, cond, ticks)
 * @brief Persistent variant of PT_YIELD_UNTIL_TIMEOUT
 *
 * Behaves like PT_YIELD_UNTIL_TIMEOUT but uses
 * PT_YIELD_UNTIL_PERSISTENT, so the continuation point is checkpointed
 * to NVM and the process is reported as "ready" rather than "waiting"
 * while the wait is in progress.  Same post-reboot semantics as
 * PT_WAIT_UNTIL_TIMEOUT_PERSISTENT -- the timer is gone after a power
 * cycle and the macro falls through as if it had timed out.
 *
 * Available only when TIKU_LC_PERSISTENT is defined.
 *
 * @param pt    Pointer to the protothread control block
 * @param timer Pointer to a caller-owned struct tiku_timer
 * @param cond  Boolean expression re-evaluated on each schedule
 * @param ticks Timeout duration in clock ticks
 */
#define PT_YIELD_UNTIL_TIMEOUT_PERSISTENT(pt, timer, cond, ticks)      \
  do {                                                                  \
    tiku_timer_set_event((timer), (ticks));                            \
    PT_YIELD_UNTIL_PERSISTENT((pt),                                    \
                              (cond) || tiku_timer_expired(timer));    \
    tiku_timer_stop(timer);                                            \
  } while (0)

#endif /* TIKU_LC_PERSISTENT */

#endif /* TIKU_TIMER_H_ */
