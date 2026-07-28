/*
 * Tiku Operating System v0.06
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

/**
 * @brief STIMER count at the last ACCOUNTED tick boundary.
 *
 * The single source of truth for tick accounting: every accounting
 * point (the compare-B ISR, an early tickless wake, the htimer
 * one-shot ISR during a stretch) derives elapsed whole ticks from
 * (counter - anchor) / period and advances the anchor by exactly the
 * credited counts.  This is wrap-safe 32-bit unsigned math (the
 * STIMER wraps every ~36 h; deltas stay correct), self-healing after
 * any latency, and — unlike the old fixed-delta re-arm — phase-locks
 * the tick to the crystal, eliminating the per-re-arm drift the old
 * comment accepted.
 */
static uint32_t s_tick_anchor;

/** @brief Non-zero while a tickless stretch window is open. */
static volatile uint8_t s_stretched;

/** @brief Current STIMER timebase rate in Hz (32768 on XTAL; measured on LFRC). */
static uint32_t s_stimer_hz = 32768u;

/** @brief Kernel tick rate in Hz, captured at tick_start so a timebase
 *  reclock can recompute s_tick_period for the new clock. */
static uint32_t s_tick_rate_hz = 128u;

/** @brief Core clock query for the LFRC calibration (tiku_cpu_freq_boot_arch.c). */
extern unsigned long tiku_cpu_ambiq_clock_get_hz(void);

/** @brief Advance the kernel tick counters; provided by tiku_timer_arch.c. */
extern void tiku_ambiq_tick_advance(void);
extern void tiku_ambiq_tick_advance_n(unsigned long n);

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
 * @brief Credit every whole tick that has elapsed since the anchor.
 *
 * Reads the free-running counter, converts the distance from
 * s_tick_anchor into whole ticks, credits them to the kernel clock in
 * one call, and advances the anchor by exactly the credited counts
 * (the sub-tick remainder stays in the anchor, preserving phase).
 * Idempotent — calling it twice in a row credits nothing the second
 * time — so every wake path may call it defensively.  Must run with
 * interrupts masked (ISR context, or inside the scheduler's atomic
 * idle section).
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
 * @brief Re-arm compare-B for the next tick boundary after the anchor.
 *
 * delta = period - (counter - anchor), floored to 1: the next tick
 * interrupt lands on the crystal-locked boundary rather than a fixed
 * period from "now", so accounting and cadence stay phase-aligned.
 */
static void stimer_tick_rearm_boundary(void) {
    uint32_t into  = stimer_counter() - s_tick_anchor;
    uint32_t delta = (into >= s_tick_period) ? 1u : (s_tick_period - into);
    stimer_arm(&STIMER->SCMPR1, delta);
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
 * htimer layer.  During a tickless stretch the kernel clock is
 * resynced FIRST, so an htimer callback that reads tiku_clock_time()
 * never sees a value stale by the stretch length.
 */
void tiku_ambiq_stimer_cmpr0_isr(void) {
    STIMER->STMINTCLR = STIMER_INT_COMPAREA;
    if (s_stretched) {
        stimer_tick_account();
    }
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
    s_tick_rate_hz = 32768u / s_tick_period;      /* tick_start runs on XTAL */
    if (s_tick_rate_hz == 0u) { s_tick_rate_hz = 1u; }

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
 * Clears the COMPAREB flag, credits every whole tick elapsed since the
 * anchor (one on the normal cadence; the full stretch after a tickless
 * sleep — the anchor math never assumes which compare fired, so a
 * one-tick match that was already latched when a stretch was being
 * armed still accounts correctly), closes any stretch window, and
 * re-arms compare-B at the next crystal-locked tick boundary.
 */
void tiku_ambiq_stimer_cmpr1_isr(void) {
    STIMER->STMINTCLR = STIMER_INT_COMPAREB;
    stimer_tick_account();
    s_stretched = 0u;
    stimer_tick_rearm_boundary();
}

/*---------------------------------------------------------------------------*/
/* TIMEBASE RECLOCK -- deep sleep support                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bounded check that the STIMER counter is actually advancing.
 *
 * A clock is trusted only after it is seen counting -- the lesson of the
 * deep-sleep autorun, whose first versions busy-polled a dead counter
 * forever.  Exits as soon as the counter moves; the bound covers several
 * counts even at the ~900 Hz LFRC rate.
 *
 * @return 1 if the counter advanced, 0 if it is frozen
 */
static int stimer_verify_counting(void) {
    uint32_t c0 = stimer_counter();
    uint32_t spin = 4000000u;
    while (stimer_counter() == c0 && --spin != 0u) { }
    return stimer_counter() != c0;
}

/**
 * @brief Measure the actual LFRC rate against the DWT cycle counter.
 *
 * The datasheet calls the LFRC "approximately 900 Hz (uncalibrated)" -- a
 * rate that wide cannot be assumed, only measured.  Times 4 LFRC counts
 * against DWT CYCCNT at the known core clock; enables TRCENA/CYCCNT
 * transiently and restores both (the boot tidy leaves TRCENA off).
 * All waits are bounded.  Runs with interrupts masked (caller's section).
 *
 * @return measured Hz, clamped to nominal 900 if implausible
 */
static uint32_t stimer_lfrc_calibrate(void) {
    volatile uint32_t *demcr  = (volatile uint32_t *)0xE000EDFCUL;
    volatile uint32_t *dwtctl = (volatile uint32_t *)0xE0001000UL;
    volatile uint32_t *cyccnt = (volatile uint32_t *)0xE0001004UL;
    uint32_t demcr0 = *demcr, ctl0 = *dwtctl;
    uint32_t c0, cyc0, cyc1, hz, spin;
    unsigned long core = tiku_cpu_ambiq_clock_get_hz();

    *demcr |= (1u << 24);            /* TRCENA   */
    *dwtctl |= 1u;                   /* CYCCNTENA */

    spin = 8000000u;                                 /* edge-align */
    c0 = stimer_counter();
    while (stimer_counter() == c0 && --spin != 0u) { }
    cyc0 = *cyccnt;
    c0 = stimer_counter();
    spin = 32000000u;                                /* 4 counts ~ 4.4 ms */
    while ((uint32_t)(stimer_counter() - c0) < 4u && --spin != 0u) { }
    cyc1 = *cyccnt;

    *dwtctl = ctl0;
    *demcr  = demcr0;

    hz = (cyc1 != cyc0)
             ? (uint32_t)(((uint64_t)core * 4u) / (uint32_t)(cyc1 - cyc0))
             : 0u;
    if (hz < 500u || hz > 2000u) {
        hz = 900u;                   /* implausible measurement: use nominal */
    }
    return hz;
}

/**
 * @brief Switch the STIMER timebase between the 32 kHz crystal and the LFRC.
 *
 * EXISTS BECAUSE THE CRYSTAL DIES UNDER REAL DEEP SLEEP on this rig (the
 * software-override XTAL enable does not survive debugger-free SLEEPDEEP;
 * measured: the deep-sleep autorun's STIMER froze and its tick-stretched
 * sleep never woke).  The LFRC keeps running, so the deep path reclocks to
 * it around the sleep window and back afterwards.
 *
 * VERIFIED SWITCH: the new source must be seen counting or the function
 * reverts to the crystal and reports failure -- never trades a working
 * timebase for a dead one.  Accounts elapsed ticks at the old rate first,
 * then re-anchors and re-arms the tick at the new rate, so kernel time
 * stays continuous across the switch.  Refuses while a tickless stretch is
 * open (the stretch compare is armed in old-clock counts); callers reclock
 * FIRST, then stretch.
 *
 * @param use_lfrc  non-zero: XTAL -> LFRC (rate measured); zero: back to XTAL
 * @return the new timebase rate in Hz, or 0 on failure (reverted to XTAL)
 */
uint32_t tiku_ambiq_stimer_reclock(int use_lfrc) {
    uint32_t primask, hz;
    uint32_t clksel = use_lfrc ? 6u /* LFRC_NOMINAL */
                               : STIMER_CLKSEL_XTAL_32KHZ;

    if (s_stretched) {
        return 0u;
    }

    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    __asm__ volatile ("cpsid i" ::: "memory");

    stimer_tick_account();                 /* settle time at the OLD rate */

    if (!use_lfrc) {
        stimer_xtal_enable();              /* re-assert the SWE override  */
    }
    STIMER->STCFG = (STIMER->STCFG & ~0xFu) | clksel;

    if (!stimer_verify_counting()) {
        STIMER->STCFG = (STIMER->STCFG & ~0xFu) | STIMER_CLKSEL_XTAL_32KHZ;
        (void)stimer_verify_counting();
        if ((primask & 1u) == 0u) {
            __asm__ volatile ("cpsie i" ::: "memory");
        }
        return 0u;
    }

    hz = use_lfrc ? stimer_lfrc_calibrate() : 32768u;

    s_stimer_hz   = hz;
    /* ROUND, don't truncate: at 884 Hz / 128 ticks the true period is 6.91
     * counts; floor(6) ran the tick 15 % fast and ended every tickless
     * stretch early (measured: a 3 s LFRC window woke 58 times -- the
     * stretch expired at 2.6 s and the remainder ran at per-tick cadence).
     * Nearest (7) is 1.3 % slow -- the best an integer period can do at
     * this granularity.  Exact on the crystal (32768/128 = 256). */
    s_tick_period = (hz + s_tick_rate_hz / 2u) / s_tick_rate_hz;
    if (s_tick_period == 0u) { s_tick_period = 1u; }
    s_tick_anchor = stimer_counter();
    /* The verify above burned >= 1 count of the NEW clock since the last
     * compare write, so the inter-write spacing is already satisfied --
     * back-date s_last_cmpr so the re-arm does not spin a full count. */
    s_last_cmpr   = s_tick_anchor - 2u;
    stimer_tick_rearm_boundary();

    if ((primask & 1u) == 0u) {
        __asm__ volatile ("cpsie i" ::: "memory");
    }
    return hz;
}

/** @brief Current STIMER timebase rate in Hz. */
uint32_t tiku_ambiq_stimer_rate_hz(void) {
    return s_stimer_hz;
}

/*---------------------------------------------------------------------------*/
/* TICKLESS IDLE — strong overrides of the kernel's weak defaults            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Stretch compare-B straight to the next software-timer deadline.
 *
 * Called by the scheduler with interrupts masked, timers armed, none
 * due.  Re-targets SCMPR1 from "next tick boundary" to "the boundary
 * @p ticks_ahead ticks after the anchor", opens the stretch window,
 * and lets the WFI idle sleep through every skipped tick.  The
 * always-on 32 kHz STIMER keeps counting through deepsleep, so the
 * resync on wake (ISR or tiku_clock_tickless_end()) is exact.
 *
 * @param ticks_ahead Ticks to the earliest deadline (>1; the 16-bit
 *                    tiku_clock_time_t bounds the stretch at 65535
 *                    ticks — ~512 s — well inside 32-bit delta range)
 * @return 1 (stretch armed)
 */
int tiku_clock_tickless_begin(tiku_clock_time_t ticks_ahead) {
    uint32_t into, span, delta;

    /* A stretch is a promise to sleep until the far compare fires; on a
     * frozen timebase that compare never comes and the sleep has no alarm
     * (the deep-sleep autorun measured exactly this: the crystal died under
     * real SLEEPDEEP and the stretched sleep never woke).  Refuse to open a
     * stretch on a clock that is not visibly counting -- the caller falls
     * back to the per-tick cadence, which at worst wastes wakes rather than
     * sleeping forever.  Cost when healthy: ~one timebase count (~30 us on
     * the crystal), only on entries that would stretch. */
    if (!stimer_verify_counting()) {
        return 0;
    }

    /* Deliberately NO accounting here: crediting a passed boundary
     * would post the timer poll (sched_notify) AFTER the scheduler
     * already checked has_pending — and the WFI would then sleep on
     * queued work.  The target is anchor-relative, so the math is
     * right either way: ticks_ahead is in units of the (possibly
     * stale) accounted tick, and the deadline boundary sits at
     * anchor + ticks_ahead * period in counts.  If a boundary HAS
     * passed, its compare-B interrupt is already pended (interrupts
     * are masked in the scheduler's idle section), the WFI falls
     * straight through, and tiku_clock_tickless_end() credits it —
     * nothing is lost, nothing sleeps on pending work. */
    into  = stimer_counter() - s_tick_anchor;
    span  = (uint32_t)ticks_ahead * s_tick_period;
    delta = (span > into) ? (span - into) : 1u;        /* to the target */

    s_stretched = 1u;
    stimer_arm(&STIMER->SCMPR1, delta);
    return 1;
}

/**
 * @brief Close the stretch window after the idle hook returns.
 *
 * Runs with interrupts still masked (inside the scheduler's atomic
 * idle section).  If the stretched compare already fired, its ISR
 * resynced the clock and restored the cadence — nothing to do.  On an
 * early wake by any other interrupt, credit the elapsed whole ticks
 * and re-arm the per-tick cadence at the next boundary.
 */
void tiku_clock_tickless_end(void) {
    if (!s_stretched) {
        return;
    }
    s_stretched = 0u;
    stimer_tick_account();
    stimer_tick_rearm_boundary();
}

/** @brief Tickless backend present (this file). */
int tiku_clock_tickless_available(void) {
    return 1;
}
