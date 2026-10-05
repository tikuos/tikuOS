/*
 * Tiku Operating System v0.06
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
 * @brief Unified software timer structure.
 *
 * In EVENT mode the process field says where the expiration goes; in CALLBACK
 * mode the func and ptr fields say what runs.
 */
struct tiku_timer {
  struct tiku_timer *next; /**< Linked list pointer (internal) */

  /* Timing state */
  tiku_clock_time_t start;    /**< Tick the interval counts from */
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
 * Empties the active list and starts the internal timer management process.
 *
 * @note Call once during system init, after tiku_process_init().
 */
void tiku_timer_init(void);

/**
 * @brief Remove every timer @p owner set (internal lifecycle hook).
 *
 * tiku_process_exit() calls it inside its atomic section, before supervision
 * can start the owner again.
 */
void tiku_timer_cancel_process(const struct tiku_process *owner);

/**
 * @brief Set a callback timer
 * @param t     Timer structure (caller-owned, must persist)
 * @param ticks Interval in clock ticks
 * @param func  Function to call on expiration
 * @param ptr   User data passed to func
 *
 * If the timer is already active, it is stopped and re-set.  The callback
 * runs in the timer process with tiku_current_process set to the process
 * that called this function (NULL when called outside one).
 *
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
 * Posts TIKU_EVENT_TIMER to the calling process when the timer expires, with
 * `t` as the event data.  A full event queue leaves the timer armed until a
 * later poll can post it; the owner's exit cancels the timer and its event.
 *
 * @code
 *   static struct tiku_timer my_timer;
 *   tiku_timer_set_event(&my_timer, TIKU_CLOCK_SECOND);
 *   TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
 * @endcode
 *
 * @note Called outside a process, the timer expires without posting.
 */
void tiku_timer_set_event(struct tiku_timer *t, tiku_clock_time_t ticks);

/**
 * @brief Reset timer for drift-free periodic operation
 * @param t Timer structure
 *
 * Re-adds the timer with start = old_start + interval, keeping the same
 * mode, callback and process, so periods do not accumulate drift.  A timer
 * more than one interval late is due again at once.
 *
 * @note Callable from the timer's own callback.
 */
void tiku_timer_reset(struct tiku_timer *t);

/**
 * @brief Restart timer from current time
 * @param t Timer structure
 *
 * Sets start to now, keeping interval, mode, callback and process: a timeout
 * re-armed on activity, for example.
 */
void tiku_timer_restart(struct tiku_timer *t);

/**
 * @brief Stop a timer
 * @param t Timer structure
 *
 * Removes the timer from the active list; does nothing for an inactive one.
 * A TIKU_EVENT_TIMER it posted before the stop is still in the queue and is
 * delivered.
 */
void tiku_timer_stop(struct tiku_timer *t);

/**
 * @brief Check whether a timer is not currently in the active list.
 * @param t Timer structure
 * @return Non-zero if the timer is inactive, zero if it is pending.
 *
 * A timer is inactive when never set, when stopped and once it has fired
 * (its event posted or its callback run); this call cannot tell these apart.
 */
int tiku_timer_expired(struct tiku_timer *t);

/**
 * @brief Get remaining time until expiration.
 * @param t Timer structure
 * @return Ticks until expiry; 0 for an inactive timer (never set, stopped or
 *         fired) and for one that is due and still in the active list.
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
 * @brief Check if any software timer is due right now.
 *
 * A timer armed for later does not count, so the scheduler may enter a
 * tick-woken idle while one waits.  Nor does a due timer whose owner the
 * memory-reclaim gate holds.
 *
 * @return Non-zero if a timer in the active list is due and not held
 */
int tiku_timer_work_pending(void);

/**
 * @brief Check whether a process owns at least one armed timer.
 *
 * The dispatcher uses it to label a blocked process for /proc and `ps`:
 * SLEEPING when it owns an armed timer, WAITING when it does not.
 *
 * @param p Process to look up (NULL matches timers set outside any
 *          process context)
 * @return Non-zero if an active timer's owner is @p p
 */
int tiku_timer_owner_armed(const struct tiku_process *p);

/**
 * @brief Return the number of active software timers.
 */
uint8_t tiku_timer_count(void);

/**
 * @brief Return the total number of timer expirations since boot; wraps at
 *        65535.
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
 * Timers are ranked by expiration minus now in the clock's width, so an
 * overdue timer's distance wraps and ranks after every future one.  The
 * tickless idle uses it to choose how far to stretch the tick.
 */
tiku_clock_time_t tiku_timer_next_expiration(void);

/**
 * @brief Ask the timer process to scan, when any timer exists (called from
 *        the tick and from tiku_crit_end())
 */
void tiku_timer_request_poll(void);

/*---------------------------------------------------------------------------*/
/* SYSTEM PROCESS                                                            */
/*---------------------------------------------------------------------------*/

/** The single timer management process */
extern struct tiku_process tiku_timer_process;

/*---------------------------------------------------------------------------*/
/* TIME CONSTANTS                                                            */
/*---------------------------------------------------------------------------*/

/** One second in timer ticks */
#define TIKU_TIMER_SECOND TIKU_CLOCK_SECOND

/** One minute in timer ticks */
#define TIKU_TIMER_MINUTE (TIKU_CLOCK_SECOND * 60UL)

/*---------------------------------------------------------------------------*/
/* TIMEOUT HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/*
 * These macros arm a one-shot tiku_timer_set_event(), wait with
 * PT_WAIT_UNTIL or PT_YIELD_UNTIL for the condition or the timer, and
 * then stop the timer.
 *
 * Caller-side contract:
 *
 *   - The caller owns a `struct tiku_timer` (typically a static
 *     variable in the process file), one per call site, and passes
 *     its address to the macro.
 *
 *   - After the macro, the caller tests the condition again: false
 *     means the wait timed out.  Stopping the timer prevents a later
 *     post, but a TIKU_EVENT_TIMER queued before the wait ended is
 *     still delivered.
 *
 *   - The condition expression is re-evaluated whenever the process
 *     is re-scheduled, as with plain PT_WAIT_UNTIL.
 *
 * Example:
 * @code
 *   static struct tiku_timer t;
 *
 *   PT_WAIT_UNTIL_TIMEOUT(pt, &t, sensor_ready(),
 *                         TIKU_CLOCK_SECOND * 2);
 *   if (sensor_ready()) {
 *       read_sensor();
 *   } else {
 *       report_timeout();
 *   }
 * @endcode
 */

/**
 * @def PT_WAIT_UNTIL_TIMEOUT(pt, timer, cond, ticks)
 * @brief Block until @p cond is true or @p ticks have elapsed.
 *
 * Sets a one-shot event timer, blocks the protothread -- the process reads as
 * "sleeping" in /proc -- until the condition holds or the timer fires, then
 * stops the timer.
 *
 * @param pt    Pointer to the protothread control block
 * @param timer Pointer to a caller-owned struct tiku_timer
 * @param cond  Boolean expression re-evaluated on each schedule
 * @param ticks Timeout duration in clock ticks
 */
#define PT_WAIT_UNTIL_TIMEOUT(pt, timer, cond, ticks)                  \
  do {                                                                  \
    tiku_timer_set_event((timer), (ticks));                            \
    PT_WAIT_UNTIL((pt), (cond) || tiku_timer_expired(timer));          \
    tiku_timer_stop(timer);                                            \
  } while (0)

/**
 * @def PT_YIELD_UNTIL_TIMEOUT(pt, timer, cond, ticks)
 * @brief Yield until @p cond is true or @p ticks have elapsed.
 *
 * Like PT_WAIT_UNTIL_TIMEOUT, but it yields at least once, even when @p cond
 * already holds, and the process reads as "ready" while it waits.  Either
 * form runs again on its next event, the timer's at the latest.
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
 * @brief Persistent variant of PT_WAIT_UNTIL_TIMEOUT.
 *
 * The continuation point is checkpointed to NVM.  A resume after a power cycle
 * enters past tiku_timer_set_event(), so the timer is unarmed, reads as
 * expired, and the macro falls through as a timeout; re-check @p cond.
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
 * @brief Persistent variant of PT_YIELD_UNTIL_TIMEOUT.
 *
 * Like PT_YIELD_UNTIL_TIMEOUT, with the continuation point checkpointed to
 * NVM.  A resume after a power cycle finds the timer unarmed, and the macro
 * falls through as a timeout.
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
