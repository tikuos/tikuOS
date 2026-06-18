/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_apollo4l.c - Apollo4 Lite hardware one-shot timer (STIMER)
 *
 * Mirrors arch/ambiq/tiku_htimer_arch.c (Apollo510). The Apollo4 Lite STIMER is
 * register-identical: STCFG/STTMR/SCMPR0/STMINT*, CLKSEL=3 (XTAL 32 kHz), the
 * COMPAREA interrupt on NVIC IRQ 32 (STIMER_CMPR0_IRQn), and the MCUCTRL.XTALCTRL
 * crystal-enable fields all match -- only the register header differs. The
 * compare register takes a DELTA (the hardware adds the counter); the async
 * 32 kHz counter is triple-read and voted; COMPARE writes are spaced.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "tiku_htimer_config.h"
#include "kernel/timers/tiku_htimer.h"
#include "apollo4l.h"       /* CMSIS register map (STIMER, MCUCTRL) -- register header only */

#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
#define AMBIQ_IRQ_STIMER_CMPR0  32

/** STIMER STCFG: CLKSEL=3 selects XTAL_32KHZ; COMPAREAEN=bit 8 */
#define STIMER_CLKSEL_XTAL_32KHZ  3u
#define STIMER_COMPAREAEN         (1u << 8)
#define STIMER_INT_COMPAREA       (1u << 0)   /* STMINT{EN,STAT,CLR}.COMPAREA */

/** @brief Counter snapshot at the last COMPARE write, to space the next write */
static uint32_t s_last_cmpr;

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
 * @brief Initialize the STIMER and enable the NVIC compare-0 interrupt (IRQ 32).
 *
 * Powers the crystal, free-runs the STIMER from it with compare-A enabled,
 * clears any stale flag, and enables IRQ 32. The COMPAREA source stays masked
 * at STMINTEN until a compare is scheduled.
 */
void tiku_htimer_arch_init(void) {
    stimer_xtal_enable();

    STIMER->STCFG     = STIMER_CLKSEL_XTAL_32KHZ | STIMER_COMPAREAEN;
    STIMER->STMINTCLR = STIMER_INT_COMPAREA;
    s_last_cmpr = stimer_counter();

    NVIC_ISER[AMBIQ_IRQ_STIMER_CMPR0 >> 5] = (1u << (AMBIQ_IRQ_STIMER_CMPR0 & 31u));
}

/**
 * @brief Schedule an STIMER compare-A interrupt at the given 16-bit tick.
 *
 * Converts the absolute target into the DELTA the hardware requires, adjusts
 * for write/interrupt latency + elapsed time, floors to 1, spaces from the
 * previous COMPARE write, all inside a PRIMASK critical section.
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

    STIMER->SCMPR0   = delta;          /* DELTA write -- hardware adds the counter */
    s_last_cmpr      = stimer_counter();
    STIMER->STMINTEN = STIMER_INT_COMPAREA;

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
