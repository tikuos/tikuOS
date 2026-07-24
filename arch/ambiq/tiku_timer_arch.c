/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - Apollo 510 system tick (always-on STIMER)
 *
 * On Ambiq the Cortex-M SysTick freezes during WFI sleep -- its clock is gated --
 * so a WFI idle with only SysTick armed never wakes, and the tick does not
 * advance while the core is parked (verified on hardware on Apollo4 Lite, the
 * same trait on this M55 part). The system tick therefore runs from the always-on
 * 32.768 kHz STIMER (compare-B / NVIC IRQ 33), which keeps running through sleep
 * and wakes the core every tick -- see tiku_htimer_arch.c, which owns the STIMER
 * and delivers the periodic interrupt into tiku_ambiq_tick_advance() below.
 * SysTick is left configured as a free-running down-counter (no TICKINT) purely so
 * the calibrated SYST_CVR micro-delay in tiku_cpu_common.c still works.
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
 *
 * These address the SysTick core peripheral at 0xE000E010. Identical
 * on every Cortex-M variant (M0/M3/M33/M55), so no AmbiqSuite dependency
 * is needed for the timer implementation.
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

/** @brief Start the periodic STIMER tick; provided by tiku_htimer_arch.c. */
extern void tiku_ambiq_stimer_tick_start(uint32_t period_counts);

/** @brief Multi-tick clock advance (defined below; used by _advance). */
void tiku_ambiq_tick_advance_n(unsigned long n);

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
 * Called from the STIMER compare-B ISR (tiku_htimer_arch.c) every tick.
 */
void tiku_ambiq_tick_advance(void) {
    tiku_ambiq_tick_advance_n(1u);
}

/**
 * @brief Advance the system clock by @p n ticks at once.
 *
 * The tickless-idle resync path (tiku_htimer_arch.c): after a
 * stretched sleep the STIMER counter says how many whole ticks really
 * elapsed, and they are credited in one call so the kernel clock is
 * exact regardless of how far the tick interrupt was stretched.
 * n == 1 is the normal per-tick cadence.  Rolls the sub-second
 * accumulator with a divide so a long stretch costs O(1).
 *
 * @param n  Whole ticks to credit (>= 1)
 */
void tiku_ambiq_tick_advance_n(unsigned long n) {
    s_ticks  += n;
    s_subsec += (unsigned int)n;
    if (s_subsec >= TIKU_CLOCK_ARCH_SECOND) {
        s_seconds += s_subsec / TIKU_CLOCK_ARCH_SECOND;
        s_subsec   = s_subsec % TIKU_CLOCK_ARCH_SECOND;
    }
    tiku_sched_notify();
}

/**
 * @brief SysTick exception handler (vector slot 15).
 *
 * Unused on Ambiq -- the tick runs off the STIMER and SysTick has no TICKINT --
 * but kept as a defensive strong override of the weak vector alias: were SysTick
 * ever armed, it advances the same clock rather than spinning.
 */
void tiku_ambiq_systick_handler(void) {
    tiku_ambiq_tick_advance();
}

/**
 * @brief Return the current tick count
 *
 * @return Monotonically increasing tick counter value
 */
tiku_clock_arch_time_t tiku_clock_arch_time(void) {
    return (tiku_clock_arch_time_t)s_ticks;
}

/**
 * @brief Return the elapsed whole-second counter
 *
 * @return Seconds elapsed since tiku_clock_arch_init()
 */
unsigned long tiku_clock_arch_seconds(void)        { return s_seconds; }

/**
 * @brief Set the whole-second counter (RTC epoch synchronisation)
 *
 * @param sec  New seconds value to load
 */
void          tiku_clock_arch_set_seconds(unsigned long sec) { s_seconds = sec; }

/**
 * @brief Spin-wait for a number of system ticks
 *
 * Busy-loops until s_ticks has advanced by at least t ticks. Uses
 * signed subtraction for correct wraparound handling.
 *
 * @param t  Number of ticks to wait
 */
void tiku_clock_arch_wait(tiku_clock_arch_time_t t) {
    tiku_clock_arch_time_t target = (tiku_clock_arch_time_t)s_ticks + t;
    while ((long)(target - (tiku_clock_arch_time_t)s_ticks) > 0) {
        /* spin -- relies on the STIMER tick advancing s_ticks */
    }
}

/**
 * @brief Busy-delay for a given number of microseconds
 *
 * Delegates to the bare-metal calibrated DWT loop in tiku_cpu_common.
 *
 * @param us  Delay duration in microseconds
 */
void tiku_clock_arch_delay(unsigned int us) {
    tiku_cpu_ambiq_delay_us(us);   /* bare-metal (calibrated DWT) */
}

/**
 * @brief Return the sub-tick fine counter (not yet modelled)
 *
 * @return 0 (coarse placeholder)
 */
unsigned short tiku_clock_arch_fine(void)     { return 0; }

/**
 * @brief Return the maximum value of the fine counter
 *
 * @return 1 (safe non-zero placeholder; sub-tick not modelled)
 */
int            tiku_clock_arch_fine_max(void) { return 1; }

/**
 * @brief Report whether the last tick had a clock fault
 *
 * @return 0 always (fault detection not implemented on this port)
 */
unsigned char  tiku_clock_arch_fault(void)    { return 0; }
