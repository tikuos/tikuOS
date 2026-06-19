/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - Apollo 510 hardware one-shot timer (STIMER)
 *                      + the always-on periodic kernel tick.
 *
 * Bare-metal driver for the Apollo510 System Timer (STIMER), clocked from the
 * 32.768 kHz crystal (TIKU_HTIMER_ARCH_SECOND). The kernel htimer's 16-bit
 * clock_t is the low 16 bits of the 32-bit counter; the compare-A interrupt
 * (NVIC IRQ 32) drives tiku_htimer_run_next(). No AmbiqSuite — this brings up
 * the crystal and the STIMER directly, transcribing the am_hal_stimer quirks:
 *   - the crystal is enabled via MCUCTRL.XTALCTRL (am_hal_mcuctrl_control's
 *     EXTCLK32K_ENABLE) — nothing else in our boot starts it;
 *   - the compare register takes a DELTA (the hardware adds the counter), NOT
 *     an absolute value;
 *   - the counter is in the async 32 kHz domain, so it is read three times
 *     and voted (am_hal_stimer_counter_get);
 *   - COMPARE writes have a 2-cycle latency and cannot be issued back-to-back,
 *     so the delta is adjusted and the write is spaced from the previous one.
 *
 * This file ALSO hosts the kernel system tick. On Ambiq the Cortex-M SysTick
 * freezes during WFI sleep (its clock is gated), so a WFI idle with only SysTick
 * armed never wakes and the tick does not advance while parked. The STIMER runs
 * from the always-on crystal and survives sleep, so the periodic tick is driven
 * here off compare-B (SCMPR1, NVIC IRQ 33) alongside the htimer's one-shot on
 * compare-A. Both compares share the single COUNTER and the inter-write spacing
 * guard (s_last_cmpr), so the tick and one-shot never corrupt each other's
 * compare writes. The tick counters live in tiku_timer_arch.c; this file only
 * delivers the periodic interrupt.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "tiku_htimer_config.h"
#include "kernel/timers/tiku_htimer.h"
#include "apollo510.h"       /* CMSIS register map (STIMER, MCUCTRL) -- register header only */

/**
 * @defgroup HTIMER_REGS STIMER and NVIC register accessors
 * @brief Direct register addresses used by the bare-metal STIMER driver.
 * @{
 */
#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
#define AMBIQ_IRQ_STIMER_CMPR0  32
#define AMBIQ_IRQ_STIMER_CMPR1  33   /**< periodic kernel tick (compare-B) */

/** STIMER STCFG (apollo510.h): CLKSEL=3 selects XTAL_32KHZ; COMPAREAEN=bit 8,
 *  COMPAREBEN=bit 9 */
#define STIMER_CLKSEL_XTAL_32KHZ  3u
#define STIMER_COMPAREAEN         (1u << 8)
#define STIMER_COMPAREBEN         (1u << 9)
#define STIMER_INT_COMPAREA       (1u << 0)   /* STMINT{EN,STAT,CLR}.COMPAREA */
#define STIMER_INT_COMPAREB       (1u << 1)   /* STMINT{EN,STAT,CLR}.COMPAREB */
/** @} */

/** @brief Counter snapshot at the last COMPARE write, to space the next write.
 *  Shared by the one-shot (SCMPR0) and the periodic tick (SCMPR1). */
static uint32_t s_last_cmpr;

/** @brief Periodic-tick reload in STIMER counts (32768 Hz / tick rate). */
static uint32_t s_tick_period;

/** @brief Advance the kernel tick counters; provided by tiku_timer_arch.c. */
extern void tiku_ambiq_tick_advance(void);

/**
 * @brief Triple-read the async 32 kHz STIMER counter and vote
 *
 * Mirrors am_hal_stimer_counter_get: if the first two reads agree,
 * neither was caught mid-ripple across the clock-domain boundary.
 * Otherwise the third read (taken after the ripple has settled) is
 * returned.
 *
 * @return Current 32-bit STIMER counter value
 */
static uint32_t stimer_counter(void) {
    uint32_t v0 = STIMER->STTMR;
    uint32_t v1 = STIMER->STTMR;
    uint32_t v2 = STIMER->STTMR;
    return (v0 == v1) ? v0 : v2;
}

/**
 * @brief Power up the 32.768 kHz crystal oscillator via MCUCTRL
 *
 * Implements the functional core of
 * am_hal_mcuctrl_control(EXTCLK32K_ENABLE): powers up the oscillator
 * core and comparator, routes through (not bypasses) the comparator,
 * and asserts the software-override enable bit.
 */
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
 * @brief Initialize the STIMER and enable the NVIC compare-0 interrupt
 *
 * Powers up the 32.768 kHz crystal, free-runs the STIMER from it with both
 * compares enabled (compare-A one-shot here, compare-B periodic tick), clears any
 * stale COMPAREA flag, and enables IRQ 32 in the NVIC. The STMINTEN compare-A
 * source is left masked here; it is armed per-schedule in
 * tiku_htimer_arch_schedule() so a stale SCMPR0 match cannot fire before the
 * first real compare. COMPAREBEN is kept set so this (later) init does not
 * disturb the periodic tick that tiku_clock_arch_init() armed earlier at boot.
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
 * @brief Schedule an STIMER compare-A interrupt at the given clock tick
 *
 * Converts the absolute 16-bit target tick @p t into the DELTA value the
 * STIMER hardware requires (it adds the current counter internally, NOT
 * an absolute). Adjusts for the 2-cycle COMPARE write latency, the 1-
 * cycle interrupt delay, and elapsed time since the snapshot. Floors the
 * delta to 1, spaces from the previous COMPARE write, all inside a PRIMASK
 * critical section. STMINTEN is OR-ed (not overwritten) so the periodic
 * tick's COMPAREB enable is preserved.
 *
 * @param t  Target 16-bit STIMER tick (absolute, wrapping)
 */
void tiku_htimer_arch_schedule(tiku_htimer_clock_t t) {
    uint32_t snap0 = stimer_counter();
    uint32_t delta = (uint32_t)(uint16_t)((uint16_t)t - (uint16_t)snap0);
    uint32_t primask, cur, guard, adj;

    if (delta == 0u) {
        delta = 1u;   /* never schedule a zero delta */
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

    STIMER->SCMPR0    = delta;          /* DELTA write — hardware adds the counter */
    s_last_cmpr       = stimer_counter();
    STIMER->STMINTEN |= STIMER_INT_COMPAREA;

    if ((primask & 1u) == 0u) {
        __asm__ volatile ("cpsie i" ::: "memory");
    }
}

/**
 * @brief Return the current 16-bit STIMER tick
 *
 * Reads the 32-bit STIMER counter via the triple-read vote and
 * returns the low 16 bits, matching the kernel htimer's clock_t width.
 *
 * @return Current 16-bit STIMER counter value
 */
tiku_htimer_clock_t tiku_htimer_arch_now(void) {
    return (tiku_htimer_clock_t)(stimer_counter() & 0xFFFFu);
}

/**
 * @brief STIMER compare-0 ISR (vector slot 16+32 in tiku_crt_early.c)
 *
 * Clears the COMPAREA pending flag and calls tiku_htimer_run_next() to
 * fire the next pending one-shot callback registered with the kernel
 * htimer layer.
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
