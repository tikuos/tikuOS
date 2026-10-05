/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_apollo4l.c - Apollo4 Lite STIMER one-shot plus the kernel tick.
 *
 * The STIMER has the Apollo510's registers; this is tiku_htimer_arch.c less
 * the timebase reclock and the counting check before a stretch.  It hosts the
 * kernel tick: SysTick stops during WFI, the STIMER does not.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "tiku_htimer_config.h"
#include "kernel/timers/tiku_htimer.h"
#include "apollo4l.h"       /* CMSIS register map (STIMER, MCUCTRL) */

#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
#define AMBIQ_IRQ_STIMER_CMPR0  32
#define AMBIQ_IRQ_STIMER_CMPR1  33   /**< periodic kernel tick (compare-B) */

/** STIMER STCFG: CLKSEL=3 selects XTAL_32KHZ; COMPAREAEN bit 8, COMPAREBEN 9 */
#define STIMER_CLKSEL_XTAL_32KHZ  3u
#define STIMER_COMPAREAEN         (1u << 8)
#define STIMER_COMPAREBEN         (1u << 9)
#define STIMER_INT_COMPAREA       (1u << 0)   /* STMINT{EN,STAT,CLR}.COMPAREA */
#define STIMER_INT_COMPAREB       (1u << 1)   /* STMINT{EN,STAT,CLR}.COMPAREB */

/** @brief Counter snapshot at the last COMPARE write, to space the next write.
 *  Shared by the one-shot (SCMPR0) and the periodic tick (SCMPR1). */
static uint32_t s_last_cmpr;

/** @brief Periodic-tick reload in STIMER counts (32768 Hz / tick rate). */
static uint32_t s_tick_period;

/**
 * @brief STIMER count at the last accounted tick boundary (tickless anchor).
 *
 * Every accounting point derives elapsed whole ticks from stimer_counter() -
 * anchor and advances the anchor by the counts it credits, so the tick stays
 * locked to the crystal.
 */
static uint32_t s_tick_anchor;

/** @brief Non-zero while a tickless stretch window is open. */
static volatile uint8_t s_stretched;

/** @brief Advance the kernel tick by one; in tiku_timer_apollo4l.c. */
extern void tiku_ambiq_tick_advance(void);
/** @brief Advance the kernel tick by @p n; in tiku_timer_apollo4l.c. */
extern void tiku_ambiq_tick_advance_n(unsigned long n);

/* Tick-accounting helpers, defined below (used by the one-shot ISR above
 * their definition, and by the tickless overrides). */
static void stimer_tick_account(void);
static void stimer_tick_rearm_boundary(void);

/** @brief Triple-read the async 32 kHz STIMER counter and vote. */
static uint32_t stimer_counter(void) {
    uint32_t v0 = STIMER->STTMR;
    uint32_t v1 = STIMER->STTMR;
    uint32_t v2 = STIMER->STTMR;
    return (v0 == v1) ? v0 : v2;
}

/** @brief Power up the 32.768 kHz crystal oscillator via MCUCTRL.XTALCTRL. */
static void stimer_xtal_enable(void) {
    MCUCTRL->XTALCTRL_b.XTALPDNB       = 1u;  /* power up XTAL core       */
    MCUCTRL->XTALCTRL_b.XTALCOMPPDNB   = 1u;  /* power up the comparator  */
    MCUCTRL->XTALCTRL_b.XTALCOMPBYPASS = 0u;  /* use the comparator       */
    MCUCTRL->XTALCTRL_b.XTALCOREDISFB  = 0u;  /* enable comparator feedbk */
    MCUCTRL->XTALCTRL_b.XTALSWE        = 1u;  /* software override enable */
}

/**
 * @brief Write a compare register with the spacing the async STIMER requires.
 *
 * Waits until the COUNTER has passed the previous compare write (tracked in
 * s_last_cmpr), writes the DELTA, and records it.  Its own PRIMASK section, so
 * it is safe from thread or ISR context and serialises SCMPR0 against SCMPR1.
 *
 * @param scmpr  Pointer to the SCMPR0/SCMPR1 compare register
 * @param delta  DELTA value (hardware adds the COUNTER); floored to 1
 */
static void stimer_arm(volatile uint32_t *scmpr, uint32_t delta) {
    uint32_t primask, cur, guard;

    if (delta == 0u) {
        delta = 1u;
    }

    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    __asm__ volatile ("cpsid i" ::: "memory");

    guard = 1000000u;
    do {
        cur = stimer_counter();
        if ((cur != s_last_cmpr) && (cur != (s_last_cmpr + 1u))) {
            break;
        }
    } while (--guard);

    *scmpr      = delta;            /* DELTA; hardware adds the counter */
    s_last_cmpr = stimer_counter();

    if ((primask & 1u) == 0u) {
        __asm__ volatile ("cpsie i" ::: "memory");
    }
}

/**
 * @brief Initialize the STIMER and enable the compare-0 interrupt (IRQ 32).
 *
 * Powers the crystal, free-runs the STIMER from it with both compares enabled,
 * clears any stale flag and enables IRQ 32.  COMPAREA stays masked at STMINTEN
 * until a compare is scheduled.
 *
 * @note COMPAREBEN is kept set so this init, which runs after
 *       tiku_clock_arch_init(), keeps the periodic tick that function armed.
 */
void tiku_htimer_arch_init(void) {
    stimer_xtal_enable();

    STIMER->STCFG     = STIMER_CLKSEL_XTAL_32KHZ |
                        STIMER_COMPAREAEN | STIMER_COMPAREBEN;
    STIMER->STMINTCLR = STIMER_INT_COMPAREA;
    s_last_cmpr = stimer_counter();

    NVIC_ISER[AMBIQ_IRQ_STIMER_CMPR0 >> 5] = (1u << (AMBIQ_IRQ_STIMER_CMPR0 & 31u));
}

/**
 * @brief Schedule an STIMER compare-A interrupt at the given 16-bit tick.
 *
 * Converts the absolute target into the DELTA the hardware requires, adjusting
 * for write/interrupt latency and elapsed time, all inside a PRIMASK section.
 * STMINTEN is OR-ed, not overwritten, so the tick's COMPAREB enable survives.
 *
 * @param t  Target 16-bit STIMER tick (absolute, wrapping)
 */
void tiku_htimer_arch_schedule(tiku_htimer_clock_t t) {
    uint32_t snap0 = stimer_counter();
    uint32_t delta = (uint32_t)(uint16_t)((uint16_t)t - (uint16_t)snap0);
    uint32_t primask, cur, guard, adj;

    if (delta == 0u) {
        delta = 1u;
    }

    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    __asm__ volatile ("cpsid i" ::: "memory");

    guard = 1000000u;
    do {
        cur = stimer_counter();
        if ((cur != s_last_cmpr) && (cur != (s_last_cmpr + 1u))) {
            break;
        }
    } while (--guard);

    adj   = 3u + (cur - snap0);
    delta = (delta > adj) ? (delta - adj) : 1u;

    STIMER->SCMPR0    = delta;          /* DELTA; hardware adds the counter */
    s_last_cmpr       = stimer_counter();
    STIMER->STMINTEN |= STIMER_INT_COMPAREA;

    if ((primask & 1u) == 0u) {
        __asm__ volatile ("cpsie i" ::: "memory");
    }
}

/** @brief Current 16-bit STIMER tick: the low 16 bits of the counter. */
tiku_htimer_clock_t tiku_htimer_arch_now(void) {
    return (tiku_htimer_clock_t)(stimer_counter() & 0xFFFFu);
}

/**
 * @brief STIMER compare-0 ISR (vector slot 16+32).
 *
 * Clears the COMPAREA flag and runs the next pending one-shot callback.
 * During a tickless stretch the kernel clock is resynced first.
 */
void tiku_ambiq_stimer_cmpr0_isr(void) {
    STIMER->STMINTCLR = STIMER_INT_COMPAREA;
    if (s_stretched) {
        stimer_tick_account();
    }
    tiku_htimer_run_next();
}

/**
 * @brief Credit every whole tick elapsed since the anchor (idempotent).
 *
 * Converts the distance from s_tick_anchor into whole ticks, credits them in
 * one call, and advances the anchor by those ticks' counts (n periods), so
 * the sub-tick remainder carries over and the tick keeps its phase.
 *
 * @note Must run with interrupts masked (ISR, or the atomic idle section).
 */
static void stimer_tick_account(void) {
    uint32_t elapsed = stimer_counter() - s_tick_anchor;
    uint32_t n = elapsed / s_tick_period;

    if (n != 0u) {
        s_tick_anchor += n * s_tick_period;
        tiku_ambiq_tick_advance_n((unsigned long)n);
    }
}

/**
 * @brief Re-arm compare-B for the next crystal-locked tick boundary.
 *
 * delta = period - (counter - anchor), floored to 1: the next tick falls on
 * the next boundary after the anchor, so the interrupts stay in phase with
 * the accounting.
 */
static void stimer_tick_rearm_boundary(void) {
    uint32_t into  = stimer_counter() - s_tick_anchor;
    uint32_t delta = (into >= s_tick_period) ? 1u : (s_tick_period - into);
    stimer_arm(&STIMER->SCMPR1, delta);
}

/**
 * @brief Start the always-on periodic kernel tick on STIMER compare-B (IRQ 33).
 *
 * Called once from tiku_clock_arch_init(), which runs before the htimer init,
 * so it does the full STIMER bring-up: crystal, free-running counter, compare-B
 * armed one period ahead, COMPAREB unmasked, NVIC IRQ 33 enabled.
 *
 * @param period_counts  STIMER counts per kernel tick (32768 / tick rate)
 */
void tiku_ambiq_stimer_tick_start(uint32_t period_counts) {
    s_tick_period = period_counts ? period_counts : 1u;

    stimer_xtal_enable();
    STIMER->STCFG     = STIMER_CLKSEL_XTAL_32KHZ |
                        STIMER_COMPAREAEN | STIMER_COMPAREBEN;
    STIMER->STMINTCLR = STIMER_INT_COMPAREA | STIMER_INT_COMPAREB;
    s_last_cmpr   = stimer_counter();
    s_tick_anchor = stimer_counter();   /* tick boundary 0 = right now */
    s_stretched   = 0u;

    stimer_arm(&STIMER->SCMPR1, s_tick_period);
    STIMER->STMINTEN |= STIMER_INT_COMPAREB;

    NVIC_ISER[AMBIQ_IRQ_STIMER_CMPR1 >> 5] = (1u << (AMBIQ_IRQ_STIMER_CMPR1 & 31u));
}

/**
 * @brief STIMER compare-1 ISR (vector slot 16+33) -- the periodic kernel tick.
 *
 * Clears the COMPAREB flag, credits every whole tick elapsed since the anchor,
 * closes any stretch window, and re-arms compare-B at the next boundary.
 */
void tiku_ambiq_stimer_cmpr1_isr(void) {
    STIMER->STMINTCLR = STIMER_INT_COMPAREB;
    stimer_tick_account();
    s_stretched = 0u;
    stimer_tick_rearm_boundary();
}

/*---------------------------------------------------------------------------*/
/* TICKLESS IDLE                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Stretch compare-B straight to the next software-timer deadline.
 *
 * Strong override; called by the scheduler with interrupts masked, timers
 * armed, none due.  Re-targets SCMPR1 to the boundary @p ticks_ahead after the
 * anchor, so the WFI sleeps through every skipped tick.
 *
 * @note Credits no ticks: a credit posts the timer poll after the scheduler
 *       checked has_pending, and the WFI then sleeps on queued work.  A
 *       boundary already passed pends its IRQ, and tiku_clock_tickless_end()
 *       credits it.
 * @param ticks_ahead Ticks to the earliest deadline (> 1)
 * @return 1 (stretch armed)
 */
int tiku_clock_tickless_begin(tiku_clock_time_t ticks_ahead) {
    uint32_t into  = stimer_counter() - s_tick_anchor;
    uint32_t span  = (uint32_t)ticks_ahead * s_tick_period;
    uint32_t delta = (span > into) ? (span - into) : 1u;

    s_stretched = 1u;
    stimer_arm(&STIMER->SCMPR1, delta);
    return 1;
}

/**
 * @brief Close the stretch window after the idle hook returns.
 *
 * Runs with interrupts still masked.  If the stretched compare already fired,
 * its ISR resynced the clock and restored the cadence.  On an early wake by any
 * other interrupt, credit the elapsed whole ticks and re-arm the cadence.
 */
void tiku_clock_tickless_end(void) {
    if (!s_stretched) {
        return;
    }
    s_stretched = 0u;
    stimer_tick_account();
    stimer_tick_rearm_boundary();
}

/** @brief Strong override: this file provides a tickless backend; returns 1. */
int tiku_clock_tickless_available(void) {
    return 1;
}
