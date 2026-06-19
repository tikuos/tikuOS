/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_apollo4l.c - Apollo4 Lite system tick (always-on STIMER)
 *
 * Apollo510 drives the system clock from the Cortex-M SysTick (see
 * tiku_timer_arch.c). On Ambiq, SysTick freezes during WFI sleep -- its clock is
 * gated -- so a WFI idle with only SysTick armed never wakes, and the tick does
 * not advance while the core is parked (verified on hardware: the deep-sleep
 * counted-idle test hangs, then under-counts elapsed ticks once force-woken).
 *
 * Apollo4 Lite therefore drives the kernel tick from the always-on 32.768 kHz
 * STIMER (compare-B / NVIC IRQ 33), which keeps running through sleep and wakes
 * the core every tick -- see tiku_htimer_apollo4l.c, which owns the STIMER and
 * delivers the periodic interrupt into tiku_ambiq_tick_advance() below. SysTick
 * is left configured as a free-running down-counter (no TICKINT) purely so the
 * calibrated SYST_CVR micro-delay in tiku_cpu_common_apollo4l.c still works.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "tiku_timer_arch.h"
#include "tiku_cpu_common.h"   /* tiku_cpu_ambiq_delay_us (bare-metal, calibrated) */
#include "kernel/scheduler/tiku_sched.h"

/**
 * @defgroup SYST Cortex-M SysTick registers (System Control Space)
 * @brief Direct-mapped SysTick CSR/RVR/CVR registers and control bits.
 * @{
 */
#define SYST_CSR  (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR  (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR  (*(volatile uint32_t *)0xE000E018UL)
#define SYST_CSR_ENABLE     (1u << 0)
#define SYST_CSR_TICKINT    (1u << 1)
#define SYST_CSR_CLKSOURCE  (1u << 2)   /* processor clock */
/** @} */

/** @brief STIMER crystal frequency (Hz) -- the periodic-tick time base. */
#define STIMER_XTAL_HZ      32768u

/** @brief Start the periodic STIMER tick; provided by tiku_htimer_apollo4l.c. */
extern void tiku_ambiq_stimer_tick_start(uint32_t period_counts);

/** @brief Monotonic tick counter incremented by each STIMER tick interrupt. */
static volatile unsigned long  s_ticks   = 0;

/** @brief Whole-second counter derived from the sub-second divider. */
static volatile unsigned long  s_seconds = 0;

/** @brief Sub-second tick accumulator; wraps at TIKU_CLOCK_ARCH_SECOND. */
static volatile unsigned int   s_subsec  = 0;

/**
 * @brief Initialize the system tick.
 *
 * Leaves SysTick free-running (ENABLE | CLKSOURCE, no TICKINT) so the SYST_CVR
 * micro-delay keeps working, then starts the always-on STIMER periodic tick at
 * TIKU_CLOCK_ARCH_SECOND Hz (STIMER_XTAL_HZ / rate counts per tick). The STIMER
 * survives WFI sleep, so the kernel clock advances and the core wakes every tick
 * even while idle-parked.
 */
void tiku_clock_arch_init(void) {
    SYST_RVR = (uint32_t)(TIKU_CLOCK_ARCH_INTERVAL - 1u);
    SYST_CVR = 0u;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_ENABLE;   /* free-run, no interrupt */

    tiku_ambiq_stimer_tick_start((uint32_t)(STIMER_XTAL_HZ / TIKU_CLOCK_ARCH_SECOND));
}

/**
 * @brief Advance the system clock by one tick.
 *
 * Called from the STIMER compare-B ISR (tiku_htimer_apollo4l.c) every tick.
 * Increments the tick + sub-second accumulators, rolls the whole-second counter,
 * and wakes the scheduler -- the same work the Apollo510 SysTick handler does.
 */
void tiku_ambiq_tick_advance(void) {
    s_ticks++;
    if (++s_subsec >= TIKU_CLOCK_ARCH_SECOND) {
        s_subsec = 0;
        s_seconds++;
    }
    tiku_sched_notify();
}

/**
 * @brief SysTick exception handler (vector slot 15).
 *
 * Unused on Apollo4 Lite -- the tick runs off the STIMER and SysTick has no
 * TICKINT -- but kept as a defensive strong override of the weak vector alias:
 * were SysTick ever armed, it advances the same clock rather than spinning.
 */
void tiku_ambiq_systick_handler(void) {
    tiku_ambiq_tick_advance();
}

/** @brief Return the current tick count. */
tiku_clock_arch_time_t tiku_clock_arch_time(void) {
    return (tiku_clock_arch_time_t)s_ticks;
}

/** @brief Return the elapsed whole-second counter. */
unsigned long tiku_clock_arch_seconds(void)        { return s_seconds; }

/** @brief Set the whole-second counter (RTC epoch synchronisation). */
void          tiku_clock_arch_set_seconds(unsigned long sec) { s_seconds = sec; }

/**
 * @brief Spin-wait for a number of system ticks.
 *
 * Busy-loops until s_ticks has advanced by at least t ticks. Uses signed
 * subtraction for correct wraparound handling.
 *
 * @param t  Number of ticks to wait
 */
void tiku_clock_arch_wait(tiku_clock_arch_time_t t) {
    tiku_clock_arch_time_t target = (tiku_clock_arch_time_t)s_ticks + t;
    while ((long)(target - (tiku_clock_arch_time_t)s_ticks) > 0) {
        /* spin -- relies on the STIMER tick advancing s_ticks */
    }
}

/** @brief Busy-delay for a given number of microseconds (calibrated DWT loop). */
void tiku_clock_arch_delay(unsigned int us) {
    tiku_cpu_ambiq_delay_us(us);
}

/** @brief Return the sub-tick fine counter (not modelled on this port). */
unsigned short tiku_clock_arch_fine(void)     { return 0; }

/** @brief Return the maximum value of the fine counter. */
int            tiku_clock_arch_fine_max(void) { return 1; }

/** @brief Report whether the last tick had a clock fault (always 0). */
unsigned char  tiku_clock_arch_fault(void)    { return 0; }
