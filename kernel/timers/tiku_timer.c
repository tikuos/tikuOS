/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer.c - Unified software timer implementation
 *
 * Single process, single linked list, handles both callback and event timers.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_timer.h"
#include "tiku.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* MODULE STATE                                                              */
/*---------------------------------------------------------------------------*/

/** Head of the active timer singly-linked list */
static struct tiku_timer *timer_list = NULL;

/** Running count of timer expirations (wraps at 65535) */
static uint16_t timer_fire_count;

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Check if clock time `now` is past the timer's expiration
 */
static inline int timer_is_due(struct tiku_timer *t, tiku_clock_time_t now) {
  return (tiku_clock_time_t)(now - t->start) >= t->interval;
}

/**
 * @brief Remove a timer from the active list (if present)
 */
static void timer_remove(struct tiku_timer *t) {
  struct tiku_timer **pp;

  for (pp = &timer_list; *pp != NULL; pp = &(*pp)->next) {
    if (*pp == t) {
      *pp = t->next;
      t->next = NULL;
      t->active = 0;
      return;
    }
  }
}

/**
 * @brief Insert a timer into the active list
 *
 * Removes first if already present (prevents duplicates),
 * then prepends to head. O(n) removal but the list is
 * typically short on embedded systems.
 */
static void timer_insert(struct tiku_timer *t) {
  /* Remove if already in list */
  if (t->active) {
    timer_remove(t);
  }

  /* Prepend */
  t->next = timer_list;
  timer_list = t;
  t->active = 1;

  /* Wake the timer process to re-evaluate next expiration */
  tiku_process_poll(&tiku_timer_process);
}

/*---------------------------------------------------------------------------*/
/* TIMER MANAGEMENT PROCESS                                                  */
/*---------------------------------------------------------------------------*/

TIKU_PROCESS(tiku_timer_process, "Timer");

TIKU_PROCESS_THREAD(tiku_timer_process, ev, data) {
  struct tiku_timer *t;
  struct tiku_timer *prev;

  TIKU_PROCESS_BEGIN();

  while (1) {
    TIKU_PROCESS_YIELD();

    /*
     * Handle process exit: remove all timers belonging to
     * the exited process.
     */
    if (ev == TIKU_EVENT_EXITED) {
      struct tiku_process *dead = data;
      struct tiku_timer **pp = &timer_list;

      while (*pp != NULL) {
        if ((*pp)->p == dead) {
          struct tiku_timer *victim = *pp;
          TIMER_PRINTF("Cleanup: removed timer for exited process\n");
          *pp = victim->next;
          victim->next = NULL;
          victim->active = 0;
        } else {
          pp = &(*pp)->next;
        }
      }
      continue;
    }

    if (ev != TIKU_EVENT_POLL) {
      continue;
    }

    /*
     * Scan for expired timers. We restart the scan after
     * each dispatch because the callback or event handler
     * might modify the list (set/stop/reset timers).
     */
  rescan:
    prev = NULL;
    for (t = timer_list; t != NULL; t = t->next) {
      if (timer_is_due(t, tiku_clock_time())) {

        /* Remove from list before dispatching */
        if (prev != NULL) {
          prev->next = t->next;
        } else {
          timer_list = t->next;
        }
        t->next = NULL;
        t->active = 0;
        timer_fire_count++;

        /* Dispatch based on mode */
        if (t->mode == TIKU_TIMER_MODE_CALLBACK && t->func != NULL) {
          TIMER_PRINTF("Expired: callback dispatched\n");
          TIKU_PROCESS_CONTEXT_BEGIN(t->p);
          t->func(t->ptr);
          TIKU_PROCESS_CONTEXT_END(t->p);
        } else if (t->mode == TIKU_TIMER_MODE_EVENT && t->p != NULL) {
          TIMER_PRINTF("Expired: event posted to %s\n", t->p->name);
          tiku_process_post(t->p, TIKU_EVENT_TIMER, t);
        }

        /* Restart scan — list may have changed */
        goto rescan;
      }
      prev = t;
    }
  }

  TIKU_PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

void tiku_timer_init(void) {
  timer_list = NULL;
  tiku_process_start(&tiku_timer_process, NULL);
  TIMER_PRINTF("Init complete\n");
}

/*---------------------------------------------------------------------------*/

void tiku_timer_set_callback(struct tiku_timer *t, tiku_clock_time_t ticks,
                             tiku_timer_callback_t func, void *ptr) {
  t->start = tiku_clock_time();
  t->interval = ticks;
  t->mode = TIKU_TIMER_MODE_CALLBACK;
  t->func = func;
  t->ptr = ptr;
  t->p = TIKU_PROCESS_CURRENT();

  TIMER_PRINTF("Set callback: interval=%u ticks\n", ticks);
  timer_insert(t);
}

/*---------------------------------------------------------------------------*/

void tiku_timer_set_event(struct tiku_timer *t, tiku_clock_time_t ticks) {
  t->start = tiku_clock_time();
  t->interval = ticks;
  t->mode = TIKU_TIMER_MODE_EVENT;
  t->func = NULL;
  t->ptr = NULL;
  t->p = TIKU_PROCESS_CURRENT();

  TIMER_PRINTF("Set event: interval=%u ticks\n", ticks);
  timer_insert(t);
}

/*---------------------------------------------------------------------------*/

void tiku_timer_reset(struct tiku_timer *t) {
  /* Drift-free: advance start by one interval from last start */
  t->start += t->interval;
  timer_insert(t);
}

/*---------------------------------------------------------------------------*/

void tiku_timer_restart(struct tiku_timer *t) {
  t->start = tiku_clock_time();
  timer_insert(t);
}

/*---------------------------------------------------------------------------*/

void tiku_timer_stop(struct tiku_timer *t) {
  TIMER_PRINTF("Stopped timer\n");
  timer_remove(t);
}

/*---------------------------------------------------------------------------*/

int tiku_timer_expired(struct tiku_timer *t) { return !t->active; }

/*---------------------------------------------------------------------------*/

tiku_clock_time_t tiku_timer_remaining(struct tiku_timer *t) {
  tiku_clock_time_t elapsed;

  if (!t->active) {
    return 0;
  }

  elapsed = tiku_clock_time() - t->start;
  if (elapsed >= t->interval) {
    return 0;
  }
  return t->interval - elapsed;
}

/*---------------------------------------------------------------------------*/

tiku_clock_time_t tiku_timer_expiration_time(struct tiku_timer *t) {
  return t->start + t->interval;
}

/*---------------------------------------------------------------------------*/

void tiku_timer_request_poll(void) { tiku_process_poll(&tiku_timer_process); }

/*---------------------------------------------------------------------------*/

int tiku_timer_any_pending(void) { return timer_list != NULL; }

/*---------------------------------------------------------------------------*/

uint8_t tiku_timer_count(void) {
    struct tiku_timer *t;
    uint8_t n = 0;
    for (t = timer_list; t != NULL; t = t->next) { n++; }
    return n;
}

uint16_t tiku_timer_fired(void) { return timer_fire_count; }

/*---------------------------------------------------------------------------*/

tiku_clock_time_t tiku_timer_next_expiration(void) {
  struct tiku_timer *t;
  tiku_clock_time_t now, nearest, dist;

  if (timer_list == NULL) {
    return 0;
  }

  now = tiku_clock_time();
  nearest = timer_list->start + timer_list->interval;
  dist = nearest - now;

  for (t = timer_list->next; t != NULL; t = t->next) {
    tiku_clock_time_t exp = t->start + t->interval;
    tiku_clock_time_t d = exp - now;
    if (d < dist) {
      dist = d;
      nearest = exp;
    }
  }

  return nearest;
}

/*---------------------------------------------------------------------------*/