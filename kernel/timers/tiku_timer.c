/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer.c - Unified software timer implementation
 *
 * Single process, single linked list, handles both callback and event timers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_timer.h"
#include "tiku_crit.h"
#include "tiku.h"
#include "kernel/memory/tiku_reclaim_internal.h"
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
 * @brief Check if clock time `now` is at or past the timer's expiration
 */
static inline int timer_is_due(struct tiku_timer *t, tiku_clock_time_t now) {
  return (tiku_clock_time_t)(now - t->start) >= t->interval;
}

/**
 * @brief Is @p t's expiration held back because the memory-reclaim gate
 *        refuses its owner a TIMER event?
 *
 * A held expiration stays armed until the gate lifts; it is not pending work,
 * so the idle loop may sleep on it.  Always 0 without TIKU_MEM_RECLAIM_ENABLE.
 */
static int timer_held(const struct tiku_timer *t) {
#if TIKU_MEM_RECLAIM_ENABLE
  return t->mode == TIKU_TIMER_MODE_EVENT && t->p != NULL &&
         !tiku_mem_reclaim_process_dispatch(t->p, TIKU_EVENT_TIMER);
#else
  (void)t;
  return 0;
#endif
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
 * An active timer is removed first, so the list holds it once; then it is
 * prepended and the timer process polled.
 */
static void timer_insert(struct tiku_timer *t) {
  if (t->active) {
    timer_remove(t);
  }

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

  (void)data;

  TIKU_PROCESS_BEGIN();

  while (1) {
    TIKU_PROCESS_YIELD();

    if (ev != TIKU_EVENT_POLL) {
      continue;
    }

    /*
     * If a critical-execution window is held, defer the scan.
     * The poll re-issued from tiku_crit_end() will pick up any
     * expirations that came due while the window was held.
     * (MSP430's tick ISR also skips the poll during a window;
     * on the other ports this check is the only one.)
     */
    if (tiku_crit_active()) {
      continue;
    }

    /*
     * Scan for expired timers.  The scan restarts after each
     * expiry, because a callback may set, stop or reset timers.
     */
  rescan:
    prev = NULL;
    for (t = timer_list; t != NULL; t = t->next) {
      if (timer_is_due(t, tiku_clock_time())) {

        /* An event the full queue refuses, or one the reclaim gate holds,
         * stays armed and is retried on the next poll; this scan moves
         * past it. */
        if (t->mode == TIKU_TIMER_MODE_EVENT && t->p != NULL &&
            (timer_held(t) || !tiku_process_post(t->p, TIKU_EVENT_TIMER, t))) {
          prev = t;
          continue;
        }

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

/* tiku_process_exit() calls this inside its atomic section, before
 * supervision can start the owner again.  The EXITED broadcast can be
 * dropped or arrive after a restart, so it cancels nothing. */
void tiku_timer_cancel_process(const struct tiku_process *owner) {
  struct tiku_timer **pp = &timer_list;
  while (*pp != NULL) {
    if ((*pp)->p == owner) {
      struct tiku_timer *t = *pp;
      *pp = t->next;
      t->next = NULL;
      t->active = 0;
    } else {
      pp = &(*pp)->next;
    }
  }
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

/*
 * Polls only while a timer exists.  The tick calls this on every interrupt;
 * with no timers it queues nothing, so the queue can stay empty and the idle
 * loop asleep.  A timer inserted after the check polls from timer_insert().
 */
void tiku_timer_request_poll(void) {
  if (timer_list != NULL) {
    tiku_process_poll(&tiku_timer_process);
  }
}

/*---------------------------------------------------------------------------*/

int tiku_timer_any_pending(void) { return timer_list != NULL; }

/*---------------------------------------------------------------------------*/

int tiku_timer_work_pending(void) {
  struct tiku_timer *t;
  tiku_clock_time_t now = tiku_clock_time();

  for (t = timer_list; t != NULL; t = t->next) {
    if (timer_is_due(t, now) && !timer_held(t)) {
      return 1;
    }
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

int tiku_timer_owner_armed(const struct tiku_process *p) {
  struct tiku_timer *t;

  for (t = timer_list; t != NULL; t = t->next) {
    if (t->p == p) {
      return 1;
    }
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

uint8_t tiku_timer_count(void) {
    struct tiku_timer *t;
    uint8_t n = 0;
    for (t = timer_list; t != NULL; t = t->next) { n++; }
    return n;
}

uint16_t tiku_timer_fired(void) { return timer_fire_count; }

struct tiku_timer *tiku_timer_get(uint8_t idx) {
    struct tiku_timer *t;
    uint8_t n = 0;
    for (t = timer_list; t != NULL; t = t->next) {
        if (n == idx) { return t; }
        n++;
    }
    return (struct tiku_timer *)0;
}

/*---------------------------------------------------------------------------*/

tiku_clock_time_t tiku_timer_next_expiration(void) {
  struct tiku_timer *t;
  tiku_clock_time_t now, nearest, dist;

  if (timer_list == NULL) {
    return 0;
  }

  now = tiku_clock_time();
  nearest = now;
  dist = 0;

  for (t = timer_list; t != NULL; t = t->next) {
    tiku_clock_time_t elapsed = (tiku_clock_time_t)(now - t->start);
    tiku_clock_time_t d;
    if (elapsed >= t->interval) {
      return now;
    }
    d = t->interval - elapsed;
    if (t == timer_list || d < dist) {
      dist = d;
      nearest = now + d;
    }
  }

  return nearest;
}

/*---------------------------------------------------------------------------*/
