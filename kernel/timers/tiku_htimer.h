/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer.h - Hardware timer abstraction interface
 *
 * ISR-driven, single-shot, microsecond precision hardware timer.
 * Only one htimer can be active at a time. Callbacks run in
 * interrupt context and may reschedule for periodic operation.
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

#ifndef TIKU_HTIMER_H_
#define TIKU_HTIMER_H_

#include <hal/tiku_htimer_hal.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @typedef tiku_htimer_clock_t
 * @brief Hardware timer tick type
 *
 * Width depends on platform (16 or 32 bits).
 * Override by defining TIKU_HTIMER_CLOCK_T before including this header.
 */
#ifndef TIKU_HTIMER_CLOCK_T_DEFINED
typedef unsigned short tiku_htimer_clock_t;
#define TIKU_HTIMER_CLOCK_T_DEFINED
#endif

/** Forward declaration for callback signature */
struct tiku_htimer;

/**
 * @typedef tiku_htimer_callback_t
 * @brief ISR callback function type
 * @param t   The htimer that fired
 * @param ptr User data pointer
 *
 * @warning Runs in interrupt context. Keep it short.
 *          May call tiku_htimer_set() to reschedule.
 */
typedef void (*tiku_htimer_callback_t)(struct tiku_htimer *t, void *ptr);

/**
 * @struct tiku_htimer
 * @brief Hardware timer instance
 */
struct tiku_htimer {
  tiku_htimer_clock_t time;    /**< Scheduled firing time (absolute ticks) */
  tiku_htimer_callback_t func; /**< ISR callback */
  void *ptr;                   /**< User data for callback */
};

/*---------------------------------------------------------------------------*/
/* RETURN CODES                                                              */
/*---------------------------------------------------------------------------*/

enum tiku_htimer_status {
  TIKU_HTIMER_OK = 0,           /**< Success */
  TIKU_HTIMER_ERR_TIME = -1,    /**< Time too close or in the past */
  TIKU_HTIMER_ERR_INVALID = -2, /**< NULL timer or callback */
  TIKU_HTIMER_ERR_NONE = -3,    /**< No timer to cancel */
};

/*---------------------------------------------------------------------------*/
/* CLOCK ARITHMETIC                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_HTIMER_CLOCK_DIFF(a, b)
 * @brief Signed difference (a - b) with wraparound handling
 */
#ifndef TIKU_HTIMER_CLOCK_DIFF
#define TIKU_HTIMER_CLOCK_DIFF(a, b) ((signed short)((a) - (b)))
#endif

/**
 * @def TIKU_HTIMER_CLOCK_LT(a, b)
 * @brief True if a is before b (handles wraparound)
 */
#define TIKU_HTIMER_CLOCK_LT(a, b) (TIKU_HTIMER_CLOCK_DIFF((a), (b)) < 0)

/*---------------------------------------------------------------------------*/
/* CORE API                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the hardware timer subsystem
 *
 * Configures platform timer hardware and enables interrupts.
 * Call once during system init.
 */
void tiku_htimer_init(void);

/**
 * @brief Schedule a hardware timer
 * @param ht   Timer structure (must remain valid until callback fires)
 * @param time Absolute tick count when timer should fire
 * @param func Callback to execute (in ISR context)
 * @param ptr  User data passed to callback
 * @return TIKU_HTIMER_OK on success, error code on failure
 *
 * Only one htimer can be active — calling this overrides any pending timer.
 * Time must be at least TIKU_HTIMER_GUARD_TIME ticks in the future.
 */
int tiku_htimer_set(struct tiku_htimer *ht, tiku_htimer_clock_t time,
                    tiku_htimer_callback_t func, void *ptr);

/**
 * @brief Schedule a single-shot hardware timer without guard-time check.
 *
 * Identical to tiku_htimer_set() but skips the
 * TIKU_HTIMER_GUARD_TIME check that rejects targets too close to now.
 *
 * Intended for tight back-to-back rescheduling from inside an ISR
 * callback where the caller has already done its own timing-margin
 * analysis (e.g. tiku_bitbang at sub-guard-time bit periods). Misuse
 * from non-ISR contexts can produce missed compares and silent drops.
 */
int tiku_htimer_set_no_guard(struct tiku_htimer *ht, tiku_htimer_clock_t time,
                             tiku_htimer_callback_t func, void *ptr);

/**
 * @brief Cancel the pending hardware timer
 * @return TIKU_HTIMER_OK if cancelled, TIKU_HTIMER_ERR_NONE if nothing pending
 */
int tiku_htimer_cancel(void);

/**
 * @brief Check if an htimer is currently pending
 * @return Non-zero if a timer is scheduled
 */
int tiku_htimer_is_scheduled(void);

/**
 * @brief Run the pending callback (called from platform ISR only)
 *
 * The platform timer interrupt handler must call this when
 * the compare-match fires. If the callback reschedules itself,
 * this function programs the next hardware interrupt automatically.
 *
 * @warning Only call from the hardware timer ISR.
 */
void tiku_htimer_run_next(void);

/*---------------------------------------------------------------------------*/
/* PLATFORM INTERFACE (implemented per-architecture)                          */
/*---------------------------------------------------------------------------*/

/** Initialize platform timer hardware */
void tiku_htimer_arch_init(void);

/** Program hardware compare register for next interrupt */
void tiku_htimer_arch_schedule(tiku_htimer_clock_t t);

/** Read current hardware timer counter */
tiku_htimer_clock_t tiku_htimer_arch_now(void);

/*---------------------------------------------------------------------------*/
/* PLATFORM CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_HTIMER_NOW()
 * @brief Read current hardware timer value
 */
#define TIKU_HTIMER_NOW() tiku_htimer_arch_now()

/**
 * @def TIKU_HTIMER_TIME(ht)
 * @brief Get the scheduled time of an htimer
 *
 * Useful for drift-free rescheduling inside a callback:
 * @code
 *   tiku_htimer_set(t, TIKU_HTIMER_TIME(t) + PERIOD, func, ptr);
 * @endcode
 */
#define TIKU_HTIMER_TIME(ht) ((ht)->time)

/**
 * @def TIKU_HTIMER_SECOND
 * @brief Hardware timer ticks per second (platform-defined)
 *
 * Provided by the arch config header as TIKU_HTIMER_ARCH_SECOND.
 */
#define TIKU_HTIMER_SECOND TIKU_HTIMER_ARCH_SECOND

/**
 * @def TIKU_HTIMER_GUARD_TIME
 * @brief Minimum ticks between now and a scheduled time
 *
 * Prevents scheduling too close to current time. Accounts for
 * ISR entry latency and register-write overhead.
 * Default: SECOND / 16384 (~61 us at 1 MHz)
 */
#ifdef TIKU_HTIMER_CONF_GUARD_TIME
#define TIKU_HTIMER_GUARD_TIME TIKU_HTIMER_CONF_GUARD_TIME
#else
#define TIKU_HTIMER_GUARD_TIME (TIKU_HTIMER_ARCH_SECOND >> 14)
#endif

#endif /* TIKU_HTIMER_H_ */
