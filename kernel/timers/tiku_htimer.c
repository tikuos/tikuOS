/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer.c - Hardware timer abstraction implementation
 *
 * Single-shot hardware timer with ISR-context callbacks.
 * Only one htimer can be pending at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <tiku.h>
#include "tiku_htimer.h"
#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* MODULE STATE                                                              */
/*---------------------------------------------------------------------------*/

/** The one pending htimer, or NULL */
static struct tiku_htimer *pending = NULL;

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the hardware timer subsystem.
 *
 * Clears any pending htimer and delegates to the arch-level init
 * which configures the Timer A1 peripheral on MSP430.
 */
void tiku_htimer_init(void) {
  pending = NULL;
  tiku_htimer_arch_init();
  HTIMER_PRINTF("htimer: initialized\n");
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Schedule a single-shot hardware timer callback.
 *
 * Validates that the target is beyond the guard time, so it cannot race the
 * hardware counter.  Only one htimer is pending at a time and a new one
 * replaces the previous; the callback runs in ISR context.
 */
int tiku_htimer_set(struct tiku_htimer *ht, tiku_htimer_clock_t time,
                    tiku_htimer_callback_t func, void *ptr) {
  tiku_htimer_clock_t now;

  /* Validate */
  if (ht == NULL || func == NULL) {
    HTIMER_PRINTF("htimer: ERR_INVALID (ht=0x%x func=0x%x)\n",
                   (unsigned int)(uintptr_t)ht,
                   (unsigned int)(uintptr_t)func);
    return TIKU_HTIMER_ERR_INVALID;
  }

  /* Guard: reject if too close to now (or wrapped past half-range) */
  now = TIKU_HTIMER_NOW();
  {
    signed short diff = TIKU_HTIMER_CLOCK_DIFF(time, now);
    if (diff < (signed short)TIKU_HTIMER_GUARD_TIME) {
      HTIMER_PRINTF("htimer: ERR_TIME (time=%u now=%u diff=%d)\n", time, now,
                    (int)diff);
      return TIKU_HTIMER_ERR_TIME;
    }
  }

  /* Configure */
  ht->time = time;
  ht->func = func;
  ht->ptr = ptr;

  /* Arm */
  pending = ht;
  tiku_htimer_arch_schedule(time);

  HTIMER_PRINTF("htimer: scheduled for %u\n", time);
  return TIKU_HTIMER_OK;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Schedule without the guard-time gate.
 *
 * For tight rescheduling paths where the caller knows the period and takes
 * responsibility for staying ahead of the counter.  Same body as
 * tiku_htimer_set() minus the guard check.
 */
int tiku_htimer_set_no_guard(struct tiku_htimer *ht, tiku_htimer_clock_t time,
                             tiku_htimer_callback_t func, void *ptr) {
  if (ht == NULL || func == NULL) {
    return TIKU_HTIMER_ERR_INVALID;
  }

  ht->time = time;
  ht->func = func;
  ht->ptr = ptr;

  pending = ht;
  tiku_htimer_arch_schedule(time);

  return TIKU_HTIMER_OK;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Cancel the pending hardware timer.
 *
 * Clears the pending pointer.  The hardware interrupt is not
 * disabled -- a spurious ISR will call run_next(), see
 * pending==NULL, and return harmlessly.
 */
int tiku_htimer_cancel(void) {
  if (pending == NULL) {
    return TIKU_HTIMER_ERR_NONE;
  }

  HTIMER_PRINTF("htimer: cancelled (was %u)\n", pending->time);
  pending = NULL;

  /*
   * The hardware interrupt is left enabled: a spurious ISR calls
   * run_next(), sees pending == NULL and returns harmlessly, which
   * avoids needing a platform-specific "disarm" function.
   */
  return TIKU_HTIMER_OK;
}

/*---------------------------------------------------------------------------*/

/** @brief Return non-zero if a hardware timer is pending. */
int tiku_htimer_is_scheduled(void) { return (pending != NULL); }

/*---------------------------------------------------------------------------*/

/**
 * @brief Execute the pending htimer callback (called from ISR).
 *
 * Grabs and clears the pending pointer before invoking the
 * callback, so the callback is free to reschedule via
 * tiku_htimer_set() without a double-fire.
 */
void tiku_htimer_run_next(void) {
  struct tiku_htimer *t;

  if (pending == NULL) {
    return;
  }

  /* Grab and clear before callback (callback may reschedule) */
  t = pending;
  pending = NULL;

  HTIMER_PRINTF("htimer: firing at %u\n", t->time);

  /* Execute in ISR context.
   * If the callback reschedules via tiku_htimer_set(), that function
   * already programs the hardware — no second arm needed here.
   */
  t->func(t, t->ptr);
}

/*---------------------------------------------------------------------------*/