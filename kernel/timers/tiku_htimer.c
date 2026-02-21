/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer.c - Hardware timer abstraction implementation
 *
 * Single-shot hardware timer with ISR-context callbacks.
 * Only one htimer can be pending at a time.
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

/**
 * @file tiku_htimer.c
 * @brief Hardware Timer Implementation
 *
 * Single-shot hardware timer with ISR-context callbacks.
 * Only one htimer can be pending at a time.
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES */
/*---------------------------------------------------------------------------*/

#include <tiku.h>
#include "tiku_htimer.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* MODULE STATE */
/*---------------------------------------------------------------------------*/

/** The one pending htimer, or NULL */
static struct tiku_htimer *pending = NULL;

/*---------------------------------------------------------------------------*/
/* PUBLIC API */
/*---------------------------------------------------------------------------*/

void tiku_htimer_init(void) {
  pending = NULL;
  tiku_htimer_arch_init();
  HTIMER_PRINTF("htimer: initialized\n");
}

/*---------------------------------------------------------------------------*/

int tiku_htimer_set(struct tiku_htimer *ht, tiku_htimer_clock_t time,
                    tiku_htimer_callback_t func, void *ptr) {
  tiku_htimer_clock_t now;

  /* Validate */
  if (ht == NULL || func == NULL) {
    HTIMER_PRINTF("htimer: ERR_INVALID (ht=%p func=%p)\n", ht, func);
    return TIKU_HTIMER_ERR_INVALID;
  }

  /* Guard: reject if too close to now */
  now = TIKU_HTIMER_NOW();
  if (TIKU_HTIMER_CLOCK_DIFF(time, now) < TIKU_HTIMER_GUARD_TIME) {
    HTIMER_PRINTF("htimer: ERR_TIME (time=%u now=%u diff=%d)\n", time, now,
                  TIKU_HTIMER_CLOCK_DIFF(time, now));
    return TIKU_HTIMER_ERR_TIME;
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

int tiku_htimer_cancel(void) {
  if (pending == NULL) {
    return TIKU_HTIMER_ERR_NONE;
  }

  HTIMER_PRINTF("htimer: cancelled (was %u)\n", pending->time);
  pending = NULL;

  /*
   * We don't disable the hardware interrupt — the spurious ISR
   * will call run_next(), see pending==NULL, and return harmlessly.
   * This avoids needing a platform-specific "disarm" function.
   */
  return TIKU_HTIMER_OK;
}

/*---------------------------------------------------------------------------*/

int tiku_htimer_is_scheduled(void) { return (pending != NULL); }

/*---------------------------------------------------------------------------*/

void tiku_htimer_run_next(void) {
  struct tiku_htimer *t;

  if (pending == NULL) {
    return;
  }

  /* Grab and clear before callback (callback may reschedule) */
  t = pending;
  pending = NULL;

  HTIMER_PRINTF("htimer: firing at %u\n", t->time);

  /* Execute in ISR context */
  t->func(t, t->ptr);

  /* If callback rescheduled, arm the hardware */
  if (pending != NULL) {
    tiku_htimer_arch_schedule(pending->time);
    HTIMER_PRINTF("htimer: rescheduled to %u\n", pending->time);
  }
}

/*---------------------------------------------------------------------------*/