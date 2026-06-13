/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - Apollo 510 hardware one-shot timer (STIMER)
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
 * The crystal has a slow (ms-to-~1 s) start-up; the system tick runs off
 * SysTick (core clock), so the htimer being cold at boot does not stall the
 * kernel — it settles before its first use (bitbang / precise rtimers).
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

/** STIMER STCFG (apollo510.h): CLKSEL=3 selects XTAL_32KHZ; COMPAREAEN=bit 8 */
#define STIMER_CLKSEL_XTAL_32KHZ  3u
#define STIMER_COMPAREAEN         (1u << 8)
#define STIMER_INT_COMPAREA       (1u << 0)   /* STMINT{EN,STAT,CLR}.COMPAREA */
/** @} */

/** @brief Counter snapshot at the last COMPARE write, to space the next write */
static uint32_t s_last_cmpr;

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
 * @brief Initialize the STIMER and enable the NVIC compare-0 interrupt
 *
 * Powers up the 32.768 kHz crystal, free-runs the STIMER from it with
 * compare-A enabled, clears any stale COMPAREA flag, and enables IRQ 32
 * in the NVIC. The STMINTEN interrupt source is left masked here; it is
 * armed per-schedule in tiku_htimer_arch_schedule() so a stale SCMPR0
 * match cannot fire before the first real compare. The crystal has a
 * slow (ms to ~1 s) start-up; the kernel SysTick runs off the core
 * clock and is therefore unaffected while the STIMER settles.
 */
void tiku_htimer_arch_init(void) {
    stimer_xtal_enable();

    /* Free-run the STIMER from the 32 kHz crystal with compare A enabled. The
     * compare-A interrupt source (STMINTEN) is armed per-schedule, NOT here, so
     * a stale SCMPR0 match can't fire before the first real compare. */
    STIMER->STCFG     = STIMER_CLKSEL_XTAL_32KHZ | STIMER_COMPAREAEN;
    STIMER->STMINTCLR = STIMER_INT_COMPAREA;
    s_last_cmpr = stimer_counter();

    /* Enable STIMER compare0 in the NVIC (IRQ 32); the source stays masked at
     * STMINTEN until a compare is scheduled. */
    NVIC_ISER[AMBIQ_IRQ_STIMER_CMPR0 >> 5] = (1u << (AMBIQ_IRQ_STIMER_CMPR0 & 31u));
}

/**
 * @brief Schedule an STIMER compare-A interrupt at the given clock tick
 *
 * Converts the absolute 16-bit target tick @p t into the DELTA value the
 * STIMER hardware requires (it adds the current counter internally, NOT
 * an absolute). Adjusts for the 2-cycle COMPARE write latency, the 1-
 * cycle interrupt delay, and elapsed time since the snapshot. Floors the
 * delta to 1 to avoid a zero-delta schedule. Guards against back-to-back
 * COMPARE writes by spin-waiting until the counter moves past the
 * previous write (bounded to prevent hang on a not-yet-stable crystal).
 * Runs inside a PRIMASK critical section to exclude the COMPAREA ISR
 * from firing mid-update.
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

    /* Critical section so the compare-A ISR can't fire mid-update. */
    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    __asm__ volatile ("cpsid i" ::: "memory");

    /* The STIMER can't take back-to-back COMPARE writes (needs ~2 cycles);
     * wait until the counter is clear of the last write. Bounded so a
     * not-yet-stable crystal (counter not advancing) cannot hang us. */
    guard = 1000000u;
    do {
        cur = stimer_counter();
        if ((cur != s_last_cmpr) && (cur != (s_last_cmpr + 1u))) {
            break;
        }
    } while (--guard);

    /* Adjust for the COMPARE write latency (2 cyc) + interrupt delay (1 cyc) +
     * the time elapsed since the snapshot; floor to 1
     * (am_hal_stimer_compare_delta_set). */
    adj   = 3u + (cur - snap0);
    delta = (delta > adj) ? (delta - adj) : 1u;

    STIMER->SCMPR0   = delta;          /* DELTA write — hardware adds the counter */
    s_last_cmpr      = stimer_counter();
    STIMER->STMINTEN = STIMER_INT_COMPAREA;

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
