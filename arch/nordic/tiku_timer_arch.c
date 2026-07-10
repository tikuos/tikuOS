/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - nRF54L system tick @ 128 Hz (GRTC, tickless-capable)
 *
 * Default source is the GRTC (Global RTC): its 1 MHz SYSCOUNTER already runs
 * (boot ROM starts it) in the always-on low-frequency domain, so the tick
 * survives deep sleep -- and, crucially, TICKLESS idle can stretch the wake
 * straight to the next software-timer deadline instead of waking 128x/second
 * to do nothing.
 *
 * Tick accounting uses a HALF-COUNT ANCHOR: 1 MHz / 128 = 7812.5 counts/tick is
 * fractional, so the anchor is tracked in half-counts (SYSCOUNTER x2), where a
 * tick is exactly 15625 half-counts.  Every accounting point (the compare ISR,
 * a tickless wake) reads the live SYSCOUNTER and advances g_ticks by the WHOLE
 * ticks elapsed since the anchor (one 64-bit divide, O(1) even across a long
 * stretch), then advances the anchor by exactly that many ticks.  So the tick
 * re-locks to the crystal and never drifts, and no tick is lost across a
 * stretched sleep.  g_seconds tracks second-boundaries crossed (preserving any
 * RTC offset from tiku_clock_arch_set_seconds()).
 *
 * Build with -DTIKU_NORDIC_TICK_TIMER10 for the bring-up fallback: a 32-bit
 * TIMER10 at 16 MHz, CC0=125000, COMPARE0->CLEAR short (exact 128 Hz too, but
 * runs off PCLK16M -- stops when HFCLK gates -- and has no tickless backend).
 *
 * SysTick is left free for busy-delays (see tiku_cpu_common.c).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_timer_arch.h"
#include <arch/nordic/mdk/nrf54l15.h>
#include <arch/nordic/tiku_nordic_core.h>
#include <arch/nordic/tiku_cpu_common.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/timers/tiku_clock.h>   /* tiku_clock_time_t + tickless decls  */
#include <kernel/cpu/tiku_hang.h>       /* tiku_hang_tick (per-tick wedge check)*/
#include <stdint.h>

/* Tick source selection: GRTC by default, TIMER10 on request. */
#if defined(TIKU_NORDIC_TICK_TIMER10)
#define TIKU_NORDIC_TICK_GRTC   0
#else
#define TIKU_NORDIC_TICK_GRTC   1
#endif

/** @brief Tick counter, incremented by the tick ISR / tickless accounting. */
static volatile tiku_clock_arch_time_t g_ticks   = 0UL;
/** @brief Seconds counter (tracks second-boundaries; RTC-settable offset). */
static volatile unsigned long          g_seconds = 0UL;

#if TIKU_NORDIC_TICK_GRTC
/*===========================================================================*/
/* GRTC tick (default) -- tickless-capable                                   */
/*===========================================================================*/

#define TIKU_GRTC             NRF_GRTC_S
#define TIKU_GRTC_IRQN        226            /* GRTC_0_IRQn (MDK enum)         */
#define TIKU_GRTC_TICK_CC     0              /* compare channel for the tick   */
#define TIKU_GRTC_CAP_TICK    2              /* capture channel for accounting */
#define TIKU_GRTC_CAP_FINE    1              /* capture channel for fine()     */
#define TIKU_GRTC_HZ          1000000UL      /* SYSCOUNTER counts at 1 MHz     */
/* Half-counts per tick: 2 * 1 MHz / 128 = 15625 (integer, exact 128 Hz). */
#define TIKU_GRTC_STEP_HALF   (2UL * TIKU_GRTC_HZ / TIKU_CLOCK_ARCH_SECOND)
#define TIKU_GRTC_CCEN_ENABLE 1UL
#define TIKU_GRTC_MODE_SYSCNTEN (1UL << 1)   /* MODE.SYSCOUNTEREN              */

/** @brief SYSCOUNTER count (x2) at the last accounted tick boundary. */
static volatile uint64_t s_anchor_half;
/** @brief Non-zero while a tickless stretch window is open. */
static volatile uint8_t  s_stretched;

/** @brief Atomically snapshot the 52-bit SYSCOUNTER via a capture channel. */
static uint64_t grtc_syscounter(uint8_t cap_ch)
{
    uint32_t lo, hi;

    TIKU_GRTC->TASKS_CAPTURE[cap_ch] = 1UL;   /* atomic snapshot into CC[ch]  */
    lo = TIKU_GRTC->CC[cap_ch].CCL;
    hi = TIKU_GRTC->CC[cap_ch].CCH;
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/**
 * @brief Credit whole ticks elapsed since the anchor; return how many.
 *
 * Reads the live SYSCOUNTER, computes the whole ticks since s_anchor_half
 * (one 64-bit divide, so O(1) whether 1 tick or thousands elapsed), advances
 * g_ticks / g_seconds / the anchor by exactly that many.  Never reentrant:
 * called only from the ISR or from tickless code with IRQs masked.
 */
static uint32_t grtc_account(void)
{
    uint64_t now_half = grtc_syscounter(TIKU_GRTC_CAP_TICK) << 1;
    uint64_t elapsed  = now_half - s_anchor_half;   /* wrap-safe (52-bit ctr) */
    uint32_t n;
    tiku_clock_arch_time_t old;

    if ((int64_t)elapsed < (int64_t)TIKU_GRTC_STEP_HALF) {
        return 0u;                                  /* less than one tick      */
    }
    n   = (uint32_t)(elapsed / TIKU_GRTC_STEP_HALF);
    old = g_ticks;
    g_ticks += (tiku_clock_arch_time_t)n;
    /* Add seconds crossed (not a recompute -- preserves any set_seconds base). */
    g_seconds += (unsigned long)((g_ticks / TIKU_CLOCK_ARCH_SECOND) -
                                 (old / TIKU_CLOCK_ARCH_SECOND));
    s_anchor_half += (uint64_t)n * TIKU_GRTC_STEP_HALF;
    return n;
}

/** @brief Arm CC0 at the next (immediate) tick boundary after the anchor. */
static void grtc_rearm_immediate(void)
{
    uint64_t next = (s_anchor_half + TIKU_GRTC_STEP_HALF) >> 1;   /* counts */

    TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCL  = (uint32_t)next;
    TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCH  = (uint32_t)(next >> 32);
    TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCEN = TIKU_GRTC_CCEN_ENABLE;
}

void tiku_clock_arch_init(void)
{
    uint64_t now;

    g_ticks     = 0UL;
    g_seconds   = 0UL;
    s_stretched = 0u;

    /* The boot ROM normally leaves the 1 MHz SYSCOUNTER running; start it if a
     * given boot path did not (never disturbs a free-running counter). */
    if ((TIKU_GRTC->MODE & TIKU_GRTC_MODE_SYSCNTEN) == 0UL) {
        TIKU_GRTC->MODE |= TIKU_GRTC_MODE_SYSCNTEN;
        TIKU_GRTC->TASKS_START = 1UL;
    }

    now = grtc_syscounter(TIKU_GRTC_CAP_TICK);
    s_anchor_half = now << 1;

    /* Disarm + clear the tick channel, then arm it at the next boundary. */
    TIKU_GRTC->INTENCLR0 = (1UL << TIKU_GRTC_TICK_CC);
    TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCEN = 0UL;
    TIKU_GRTC->EVENTS_COMPARE[TIKU_GRTC_TICK_CC] = 0UL;
    grtc_rearm_immediate();
    TIKU_GRTC->INTENSET0 = (1UL << TIKU_GRTC_TICK_CC);

    /* IRQs are still globally masked (reset handler ran cpsid i); the scheduler
     * unmasks at the top of its loop, so the first tick cannot fire early. */
    tiku_nordic_nvic_clear_pending(TIKU_GRTC_IRQN);
    tiku_nordic_nvic_set_priority(TIKU_GRTC_IRQN, 3u);
    tiku_nordic_nvic_enable(TIKU_GRTC_IRQN);
}

/**
 * @brief GRTC COMPARE0 ISR: credit elapsed ticks, re-arm, wake the poll.
 *
 * Overrides the weak alias installed by the crt vector table (IRQn 226).
 */
void tiku_nordic_grtc_isr(void)
{
    if (TIKU_GRTC->EVENTS_COMPARE[TIKU_GRTC_TICK_CC] != 0UL) {
        uint32_t n;

        TIKU_GRTC->EVENTS_COMPARE[TIKU_GRTC_TICK_CC] = 0UL;
        (void)TIKU_GRTC->EVENTS_COMPARE[TIKU_GRTC_TICK_CC];   /* flush clear */

        n = grtc_account();
        grtc_rearm_immediate();
        tiku_hang_tick();               /* per-tick wedge check (idle = NULL) */
        if (n != 0u) {
            tiku_sched_notify();
        }
    }
}

unsigned short tiku_clock_arch_fine(void)
{
    uint32_t now  = (uint32_t)grtc_syscounter(TIKU_GRTC_CAP_FINE);
    uint32_t base = (uint32_t)(s_anchor_half >> 1);   /* current boundary     */
    uint32_t pos  = now - base;                        /* counts into the tick */
    uint32_t span = (uint32_t)(TIKU_GRTC_STEP_HALF >> 1) + 1u;  /* ~7813 */

    if (pos >= span) {
        pos = span - 1u;                               /* clamp (best-effort)  */
    }
    return (unsigned short)((pos * 0xFFFFUL) / span);
}

/*---------------------------------------------------------------------------*/
/* Tickless backend (strong overrides of the weak defaults in tiku_clock.c)  */
/* Disable with -DTIKU_NORDIC_NO_TICKLESS to fall back to per-tick wakeups.   */
/*---------------------------------------------------------------------------*/
#if !defined(TIKU_NORDIC_NO_TICKLESS)

/**
 * @brief Stretch the tick compare to a deadline @p ticks_ahead ticks away.
 *
 * Called by the scheduler (IRQs masked) before an idle sleep when the earliest
 * armed timer is >1 tick out.  Points CC0 at the deadline boundary so the CPU
 * sleeps through the intervening ticks; the wake path (ISR at the deadline, or
 * tickless_end on an early non-tick wake) credits the elapsed ticks.  Does NOT
 * advance g_ticks here -- ticks_ahead is relative to the current (possibly
 * one-tick-stale) g_ticks, and the anchor is tied to it, so anchor + ticks_ahead
 * is the correct absolute deadline regardless.
 */
int tiku_clock_tickless_begin(tiku_clock_time_t ticks_ahead)
{
    uint64_t target_half = s_anchor_half +
                           (uint64_t)ticks_ahead * TIKU_GRTC_STEP_HALF;
    uint64_t target      = target_half >> 1;

    TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCL  = (uint32_t)target;
    TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCH  = (uint32_t)(target >> 32);
    TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCEN = TIKU_GRTC_CCEN_ENABLE;

    s_stretched = 1u;
    return 1;
}

/**
 * @brief Close a stretch window: credit elapsed ticks, restore per-tick cadence.
 *
 * Runs with IRQs still masked.  On a deadline wake the stretched compare is
 * pending (taken after this returns) -- grtc_account here already credits the
 * ticks, and re-arming to the immediate boundary cancels the far compare so an
 * early non-tick wake does not later fire a stale deadline.
 */
void tiku_clock_tickless_end(void)
{
    uint32_t n;

    if (!s_stretched) {
        return;
    }
    s_stretched = 0u;

    n = grtc_account();
    grtc_rearm_immediate();
    if (n != 0u) {
        tiku_sched_notify();
    }
}

/** @brief Tickless backend present. */
int tiku_clock_tickless_available(void)
{
    return 1;
}

#endif /* !TIKU_NORDIC_NO_TICKLESS */

#else /* !TIKU_NORDIC_TICK_GRTC */
/*===========================================================================*/
/* TIMER10 tick (fallback: -DTIKU_NORDIC_TICK_TIMER10, no tickless backend)  */
/*===========================================================================*/

#define TIKU_TIMER10             NRF_TIMER10_S
#define TIKU_TIMER10_IRQN        133   /* TIMER10_IRQn (MDK enum) */
#define TIKU_TIMER_HZ            16000000UL
#define TIKU_TIMER_INTERVAL      (TIKU_TIMER_HZ / TIKU_CLOCK_ARCH_SECOND)  /* 125000 */
#define TIMER_MODE_TIMER         0UL
#define TIMER_BITMODE_32BIT      3UL
#define TIMER_PRESCALER_16MHZ    0UL
#define TIMER_SHORTS_C0_CLEAR    (1UL << 0)
#define TIMER_INTENSET_COMPARE0  (1UL << 16)

void tiku_clock_arch_init(void)
{
    g_ticks   = 0UL;
    g_seconds = 0UL;

    TIKU_TIMER10->TASKS_STOP  = 1UL;
    TIKU_TIMER10->TASKS_CLEAR = 1UL;

    TIKU_TIMER10->MODE      = TIMER_MODE_TIMER;
    TIKU_TIMER10->BITMODE   = TIMER_BITMODE_32BIT;
    TIKU_TIMER10->PRESCALER = TIMER_PRESCALER_16MHZ;      /* 16 MHz          */
    TIKU_TIMER10->CC[0]     = TIKU_TIMER_INTERVAL;        /* 125000 => 128Hz */
    TIKU_TIMER10->SHORTS    = TIMER_SHORTS_C0_CLEAR;      /* auto-wrap       */

    TIKU_TIMER10->EVENTS_COMPARE[0] = 0UL;
    TIKU_TIMER10->INTENSET = TIMER_INTENSET_COMPARE0;

    tiku_nordic_nvic_clear_pending(TIKU_TIMER10_IRQN);
    tiku_nordic_nvic_set_priority(TIKU_TIMER10_IRQN, 3u);
    tiku_nordic_nvic_enable(TIKU_TIMER10_IRQN);

    TIKU_TIMER10->TASKS_START = 1UL;
}

/** @brief TIMER10 compare ISR (crt vector IRQn 133). */
void tiku_nordic_timer10_isr(void)
{
    if (TIKU_TIMER10->EVENTS_COMPARE[0] != 0UL) {
        TIKU_TIMER10->EVENTS_COMPARE[0] = 0UL;
        (void)TIKU_TIMER10->EVENTS_COMPARE[0];   /* read-back: flush clear */

        g_ticks++;
        if ((g_ticks % (tiku_clock_arch_time_t)TIKU_CLOCK_ARCH_SECOND) == 0UL) {
            g_seconds++;
        }
        tiku_hang_tick();
        tiku_sched_notify();
    }
}

unsigned short tiku_clock_arch_fine(void)
{
    uint32_t count;

    TIKU_TIMER10->TASKS_CAPTURE[1] = 1UL;
    count = TIKU_TIMER10->CC[1];
    if (count >= TIKU_TIMER_INTERVAL) {
        return 0xFFFFu;
    }
    return (unsigned short)((count * 0xFFFFUL) / TIKU_TIMER_INTERVAL);
}

#endif /* TIKU_NORDIC_TICK_GRTC */

/*===========================================================================*/
/* Shared queries (source-independent)                                       */
/*===========================================================================*/

tiku_clock_arch_time_t tiku_clock_arch_time(void)
{
    return g_ticks;
}

unsigned long tiku_clock_arch_seconds(void)
{
    return g_seconds;
}

void tiku_clock_arch_set_seconds(unsigned long sec)
{
    g_seconds = sec;
}

void tiku_clock_arch_wait(tiku_clock_arch_time_t t)
{
    /* Wraparound-safe: spin while t is still in the future. */
    while ((long)(t - g_ticks) > 0) {
        /* busy-wait for the tick ISR to advance g_ticks */
    }
}

void tiku_clock_arch_delay(unsigned int us)
{
    tiku_cpu_nordic_delay_us((uint32_t)us);
}

int tiku_clock_arch_fine_max(void)
{
    return 0xFFFF;
}
