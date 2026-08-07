/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - STM32N6 high-resolution timer on LPTIM1 compare.
 *
 * Shares LPTIM1 with the kernel tick: the counter gives the time, channel 1
 * gives the alarm, and one interrupt serves both.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/timers/tiku_htimer.h>

#include "tiku_htimer_config.h"
#include "tiku_timer_arch.h"
#include "tiku_stm32n6_regs.h"

#define LPTIM   STM32N6_LPTIM1_BASE

/* The counter runs at 500 kHz, so one count is 2 us. */
#define US_PER_COUNT    (1000000UL / TIKU_STM32N6_LPTIM_HZ)

/** @brief Alarms taken, for localising "the ISR never fired" reports. */
volatile uint32_t tiku_htimer_arch_isr_count;

/**
 * @brief Absolute alarm time in counts since boot; valid while htimer_armed.
 */
static volatile unsigned long long htimer_target;

/** @brief Whether an alarm is outstanding. */
static volatile uint8_t htimer_armed;

/** @brief Counts since boot, spanning the per-tick counter reload. */
static unsigned long long htimer_counts_now(void) {
    unsigned long ticks;
    uint32_t cnt;
    /* Re-read if the tick advanced mid-sample, or the two halves disagree. */
    do {
        ticks = tiku_clock_arch_time();
        cnt   = tiku_clock_arch_fine();
    } while (ticks != tiku_clock_arch_time());
    return ((unsigned long long)ticks * TIKU_CLOCK_ARCH_INTERVAL) + cnt;
}

void tiku_htimer_arch_init(void) {
    htimer_armed  = 0U;
    htimer_target = 0ULL;
    TIKU_REG32(STM32N6_LPTIM_DIER(LPTIM)) &= ~STM32N6_LPTIM_DIER_CC1IE;
    TIKU_REG32(STM32N6_LPTIM_ICR(LPTIM))   = STM32N6_LPTIM_ICR_CC1CF;
}

tiku_htimer_clock_t tiku_htimer_arch_now(void) {
    return (tiku_htimer_clock_t)(htimer_counts_now() * US_PER_COUNT);
}

/**
 * @brief Arm channel 1 when the alarm falls inside the current counter period.
 *
 * The counter reloads every tick, so a compare can only be programmed for the
 * period it lies in; the tick handler calls this again as each period opens.
 */
static void htimer_try_arm(void) {
    if (!htimer_armed) {
        return;
    }
    unsigned long long now = htimer_counts_now();
    unsigned long long tgt = htimer_target;

    if (tgt <= now) {
        /* Already due: fire on the next compare match by arming at the
         * current count, which matches within this period. */
        tgt = now + 1ULL;
    }
    if ((tgt - now) >= TIKU_CLOCK_ARCH_INTERVAL) {
        return;                 /* a later period; re-checked on each tick */
    }

    uint32_t within = (uint32_t)(tgt % TIKU_CLOCK_ARCH_INTERVAL);
    TIKU_REG32(STM32N6_LPTIM_ICR(LPTIM))   = STM32N6_LPTIM_ICR_CC1CF;
    TIKU_REG32(STM32N6_LPTIM_CCR1(LPTIM))  = within;
    TIKU_REG32(STM32N6_LPTIM_DIER(LPTIM)) |= STM32N6_LPTIM_DIER_CC1IE;
}

void tiku_htimer_arch_schedule(tiku_htimer_clock_t t) {
    unsigned long long now = htimer_counts_now();
    /* The kernel's clock is 16-bit microseconds; recover the signed delta and
     * project it onto the running count. */
    int16_t delta_us = (int16_t)((uint16_t)t -
                                 (uint16_t)(now * US_PER_COUNT));
    long long delta_counts = (long long)delta_us / (long long)US_PER_COUNT;

    htimer_target = (delta_counts > 0) ? (now + (unsigned long long)delta_counts)
                                       : (now + 1ULL);
    htimer_armed  = 1U;
    htimer_try_arm();
}

/**
 * @brief Called from the LPTIM1 interrupt when a compare match lands.
 *
 * Masks the compare before dispatching so a callback that does not reschedule
 * cannot re-enter on the way out.
 */
void tiku_stm32n6_htimer_on_compare(void) {
    tiku_htimer_arch_isr_count++;
    TIKU_REG32(STM32N6_LPTIM_DIER(LPTIM)) &= ~STM32N6_LPTIM_DIER_CC1IE;
    TIKU_REG32(STM32N6_LPTIM_ICR(LPTIM))   = STM32N6_LPTIM_ICR_CC1CF;
    htimer_armed = 0U;
    tiku_htimer_run_next();
}

/** @brief Called from the LPTIM1 interrupt after each tick, to re-arm. */
void tiku_stm32n6_htimer_on_tick(void) {
    htimer_try_arm();
}
