/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - RA8P1 high-resolution timer on SysTick.
 *
 * The time BASE is genuinely sub-microsecond: SysTick's current-value register
 * steps once per core clock, so tiku_htimer_arch_now() resolves ~0.12 us at
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/timers/tiku_htimer.h>

#include "tiku_htimer_config.h"
#include "tiku_timer_arch.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_ra8p1_regs.h"

/** @brief Alarms taken, for localising "the ISR never fired" reports. */
volatile uint32_t tiku_htimer_arch_isr_count;

/** @brief Absolute alarm time in counts since boot. */
static volatile unsigned long long htimer_target;

/** @brief Whether an alarm is outstanding. */
static volatile uint8_t htimer_armed;

/**
 * @brief Core-clock counts since boot, spanning the per-tick reload.
 *
 * Re-read if the tick advanced mid-sample: the two halves come from different
 * registers, and a sample straddling a reload would otherwise report a time
 * that never existed.
 */
static unsigned long long htimer_counts_now(void)
{
    tiku_clock_arch_time_t ticks;
    unsigned short cnt;

    do {
        ticks = tiku_clock_arch_time();
        cnt   = tiku_clock_arch_fine();
    } while (ticks != tiku_clock_arch_time());

    return ((unsigned long long)ticks * TIKU_CLOCK_ARCH_INTERVAL) + cnt;
}

/**
 * @brief Sub-tick counts per microsecond, at whatever the clock is NOW.
 *
 * A compile-time constant here would keep reporting boot-clock microseconds
 * after R4 raises the tree -- every measurement out by the clock ratio.
 */
static unsigned long counts_per_us(void)
{
    unsigned long hz = tiku_ra8p1_clock_arch_fine_hz() /
                       (unsigned long)TIKU_CLOCK_ARCH_SECOND *
                       (unsigned long)TIKU_CLOCK_ARCH_SECOND;

    return (hz >= 1000000UL) ? (hz / 1000000UL) : 1UL;
}

#define COUNTS_PER_US   counts_per_us()

void tiku_htimer_arch_init(void)
{
    htimer_armed  = 0U;
    htimer_target = 0ULL;
}

tiku_htimer_clock_t tiku_htimer_arch_now(void)
{
    return (tiku_htimer_clock_t)(htimer_counts_now() / COUNTS_PER_US);
}

void tiku_htimer_arch_schedule(tiku_htimer_clock_t t)
{
    unsigned long long now = htimer_counts_now();
    /* The kernel's clock is 16-bit microseconds; recover the signed delta and
     * project it onto the running count. */
    int16_t delta_us = (int16_t)((uint16_t)t -
                                 (uint16_t)(now / COUNTS_PER_US));
    long long delta = (long long)delta_us * (long long)COUNTS_PER_US;

    htimer_target = (delta > 0) ? (now + (unsigned long long)delta)
                                : (now + 1ULL);
    htimer_armed  = 1U;
}

/**
 * @brief Dispatch a due alarm; called from the tick handler.
 *
 * Disarms before dispatching so a callback that does not reschedule cannot
 * re-enter on the way out.
 */
void tiku_ra8p1_htimer_on_tick(void)
{
    if (!htimer_armed) {
        return;
    }
    if (htimer_counts_now() < htimer_target) {
        return;
    }
    tiku_htimer_arch_isr_count++;
    htimer_armed = 0U;
    tiku_htimer_run_next();
}
