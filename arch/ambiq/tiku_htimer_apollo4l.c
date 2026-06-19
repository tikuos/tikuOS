/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_apollo4l.c - Apollo4 Lite hardware one-shot timer (STIMER)
 *                          + the always-on periodic kernel tick.
 *
 * Mirrors arch/ambiq/tiku_htimer_arch.c (Apollo510). The Apollo4 Lite STIMER is
 * register-identical: STCFG/STTMR/SCMPR0/STMINT*, CLKSEL=3 (XTAL 32 kHz), the
 * COMPAREA interrupt on NVIC IRQ 32 (STIMER_CMPR0_IRQn), and the MCUCTRL.XTALCTRL
 * crystal-enable fields all match -- only the register header differs. The
 * compare register takes a DELTA (the hardware adds the counter); the async
 * 32 kHz counter is triple-read and voted; COMPARE writes are spaced.
 *
 * On Apollo4 Lite this file ALSO hosts the kernel system tick. SysTick (the tick
 * source on Apollo510, see tiku_timer_arch.c) freezes during WFI sleep on Ambiq,
 * so a WFI idle with only SysTick armed never wakes -- and the tick never
 * advances while parked. The STIMER runs from the always-on 32 kHz crystal and
 * survives sleep, so the periodic tick is driven here off compare-B (SCMPR1,
 * NVIC IRQ 33) alongside the htimer's one-shot on compare-A. Both compares share
 * the single COUNTER and the inter-write spacing guard (s_last_cmpr), so the
 * tick and one-shot never corrupt each other's compare writes. The tick counters
 * live in tiku_timer_apollo4l.c; this file only delivers the periodic interrupt.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "tiku_htimer_config.h"
#include "kernel/timers/tiku_htimer.h"
#include "apollo4l.h"       /* CMSIS register map (STIMER, MCUCTRL) -- register header only */

#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
#define AMBIQ_IRQ_STIMER_CMPR0  32
#define AMBIQ_IRQ_STIMER_CMPR1  33   /**< periodic kernel tick (compare-B) */

/** STIMER STCFG: CLKSEL=3 selects XTAL_32KHZ; COMPAREAEN=bit 8, COMPAREBEN=bit 9 */
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

/** @brief Advance the kernel tick counters; provided by tiku_timer_apollo4l.c. */
extern void tiku_ambiq_tick_advance(void);

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
 * Waits until the COUNTER has advanced past the previous compare write (tracked
 * in the shared s_last_cmpr), then writes the DELTA and records the write.
 * Self-contained PRIMASK critical section, so it is safe from either thread
 * context (htimer schedule) or ISR context (the tick re-arm), and serialises
 * the one-shot (SCMPR0) against the periodic tick (SCMPR1) on the shared bus.
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

    *scmpr      = delta;            /* DELTA write -- hardware adds the counter */
    s_last_cmpr = stimer_counter();

    if ((primask & 1u) == 0u) {
        __asm__ volatile ("cpsie i" ::: "memory");
    }
}

/**
 * @brief Initialize the STIMER and enable the NVIC compare-0 interrupt (IRQ 32).
 *
 * Powers the crystal, free-runs the STIMER from it with both compares enabled
 * (compare-A one-shot here, compare-B periodic tick), clears any stale flag, and
 * enables IRQ 32. The COMPAREA source stays masked at STMINTEN until a compare
 * is scheduled. COMPAREBEN is kept set so this (later) init does not disturb the
 * periodic tick that tiku_clock_arch_init() armed earlier at boot.
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
 * Converts the absolute target into the DELTA the hardware requires, adjusts
 * for write/interrupt latency + elapsed time, floors to 1, spaces from the
 * previous COMPARE write, all inside a PRIMASK critical section. STMINTEN is
 * OR-ed (not overwritten) so the periodic tick's COMPAREB enable is preserved.
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

    STIMER->SCMPR0    = delta;          /* DELTA write -- hardware adds the counter */
    s_last_cmpr       = stimer_counter();
    STIMER->STMINTEN |= STIMER_INT_COMPAREA;

    if ((primask & 1u) == 0u) {
        __asm__ volatile ("cpsie i" ::: "memory");
    }
}

/** @brief Return the current 16-bit STIMER tick (low 16 bits of the counter). */
tiku_htimer_clock_t tiku_htimer_arch_now(void) {
    return (tiku_htimer_clock_t)(stimer_counter() & 0xFFFFu);
}

/**
 * @brief STIMER compare-0 ISR (vector slot 16+32).
 *
 * Clears the COMPAREA flag and runs the next pending one-shot callback.
 */
void tiku_ambiq_stimer_cmpr0_isr(void) {
    STIMER->STMINTCLR = STIMER_INT_COMPAREA;
    tiku_htimer_run_next();
}

/**
 * @brief Start the always-on periodic kernel tick on STIMER compare-B (IRQ 33).
 *
 * Called once from tiku_clock_arch_init() (which runs before the htimer init at
 * boot). Brings up the crystal + free-running STIMER, arms compare-B one period
 * ahead, unmasks COMPAREB, and enables NVIC IRQ 33. Because this runs first, it
 * does the full STIMER bring-up; the later tiku_htimer_arch_init() re-asserts the
 * same STCFG (with COMPAREBEN preserved) idempotently.
 *
 * @param period_counts  STIMER counts per kernel tick (32768 / tick rate)
 */
void tiku_ambiq_stimer_tick_start(uint32_t period_counts) {
    s_tick_period = period_counts ? period_counts : 1u;

    stimer_xtal_enable();
    STIMER->STCFG     = STIMER_CLKSEL_XTAL_32KHZ |
                        STIMER_COMPAREAEN | STIMER_COMPAREBEN;
    STIMER->STMINTCLR = STIMER_INT_COMPAREA | STIMER_INT_COMPAREB;
    s_last_cmpr = stimer_counter();

    stimer_arm(&STIMER->SCMPR1, s_tick_period);
    STIMER->STMINTEN |= STIMER_INT_COMPAREB;

    NVIC_ISER[AMBIQ_IRQ_STIMER_CMPR1 >> 5] = (1u << (AMBIQ_IRQ_STIMER_CMPR1 & 31u));
}

/**
 * @brief STIMER compare-1 ISR (vector slot 16+33) -- the periodic kernel tick.
 *
 * Clears the COMPAREB flag, re-arms compare-B one period ahead (drift is a
 * fraction of one 30.5 us STIMER count -- the match-to-rearm latency -- so a
 * fixed-delta re-arm is well within tick tolerance), then advances the kernel
 * clock. Runs through the same spacing guard as the one-shot path.
 */
void tiku_ambiq_stimer_cmpr1_isr(void) {
    STIMER->STMINTCLR = STIMER_INT_COMPAREB;
    stimer_arm(&STIMER->SCMPR1, s_tick_period);
    tiku_ambiq_tick_advance();
}
