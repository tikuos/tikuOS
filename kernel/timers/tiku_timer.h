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
 * mode the func and ptr fields say what runs.  About 20-24 bytes per timer.
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
 * Inactive covers both never-set and set-fired-and-dispatched, and this call
 * cannot tell them apart; a caller needing that must track it.  The usual
 * set-then-wait pattern is unaffected, since the caller just set the timer.
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
 * @brief Check if any software timer is due right now.
 *
 * Distinct from tiku_timer_any_pending(): armed-for-later is not work, so the
 * scheduler can enter a tick-woken idle instead of spinning until expiry.
 *
 * @return Non-zero if at least one active timer has expired but has
 *         not yet been dispatched
 */
int tiku_timer_work_pending(void);

/**
 * @brief Check whether a process owns at least one armed timer.
 *
 * Used by the dispatcher to classify a blocked process as SLEEPING
 * (parked on a timer deadline) rather than WAITING (parked on an
 * event with no scheduled wake-up) for /proc and `ps`.
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
 * @brief Block until @p cond is true or @p ticks have elapsed.
 *
 * Sets a one-shot event timer, blocks the protothread -- the process reads as
 * "waiting" in /proc -- until the condition holds or the timer fires, then
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
 * As PT_WAIT_UNTIL_TIMEOUT but with yield semantics, so the process reads as
 * "ready" rather than "waiting" -- use it when the wait belongs to a polling
 * loop the scheduler should keep treating as runnable.
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
 * The continuation point is checkpointed to NVM, but the timer lives in RAM and
 * does not survive a power cycle: on resume it reads as already expired and the
 * macro falls through, so the caller must re-check @p cond and retry.
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
 * As the WAIT variant but with yield semantics, so the process reads as "ready"
 * while waiting.  Same post-reboot behaviour: the timer is gone and the macro
 * falls through as if it had timed out.
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
