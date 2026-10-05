/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - Apollo510 system tick (always-on STIMER).
 *
 * SysTick stops during WFI on Ambiq parts, so the tick runs from the 32.768 kHz
 * STIMER, which also wakes an idle core.  SysTick free-runs without an
 * interrupt as the counter of the busy-delay loop.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "tiku_timer_arch.h"
#include "tiku_cpu_common.h"   /* tiku_cpu_ambiq_delay_us() */
#include "kernel/scheduler/tiku_sched.h"

/**
 * @defgroup SYST Cortex-M SysTick registers (System Control Space)
 * @brief Direct-mapped SysTick CSR/RVR/CVR registers and control bits.
 *
 * The SysTick core peripheral at 0xE000E010, the same on every Cortex-M
 * variant (M0/M3/M33/M55).
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

/** @brief Credit @p n ticks at once; defined below. */
void tiku_ambiq_tick_advance_n(unsigned long n);

/** @brief Tick counter, advanced by tiku_ambiq_tick_advance_n(). */
static volatile unsigned long  s_ticks   = 0;

/** @brief Whole-second counter derived from the sub-second divider. */
static volatile unsigned long  s_seconds = 0;

/** @brief Sub-second tick accumulator; wraps at TIKU_CLOCK_ARCH_SECOND. */
static volatile unsigned int   s_subsec  = 0;

/**
 * @brief Initialize the system tick.
 *
 * Leaves SysTick free-running (ENABLE | CLKSOURCE, no TICKINT) for the
 * SYST_CVR busy-delay, then starts the STIMER periodic tick at
 * TIKU_CLOCK_ARCH_SECOND Hz.  The STIMER keeps counting through WFI.
 */
void tiku_clock_arch_init(void) {
    SYST_RVR = (uint32_t)(TIKU_CLOCK_ARCH_INTERVAL - 1u);
    SYST_CVR = 0u;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_ENABLE; /* free-run, no IRQ */

    tiku_ambiq_stimer_tick_start((uint32_t)(STIMER_XTAL_HZ / TIKU_CLOCK_ARCH_SECOND));
}

/**
 * @brief Advance the system clock by one tick.
 *
 * The SysTick handler below calls it.  The STIMER compare-B ISR
 * (tiku_htimer_arch.c) credits its ticks through tiku_ambiq_tick_advance_n().
 */
void tiku_ambiq_tick_advance(void) {
    tiku_ambiq_tick_advance_n(1u);
}

/**
 * @brief Advance the system clock by @p n ticks at once.
 *
 * The tickless resync credits a whole stretch in one call; the per-tick path
 * passes 1.  The sub-second accumulator rolls with a divide, so the cost does
 * not grow with @p n.  Ends by notifying the scheduler.
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
 * SysTick runs without TICKINT, so this does not run in normal operation.  It
 * overrides the weak vector alias to the default handler: an armed SysTick
 * would advance the clock by one tick per interrupt.
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
 * @return The value tiku_clock_arch_set_seconds() last loaded (0 from boot)
 *         plus the whole seconds counted since
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
 * Delegates to tiku_cpu_ambiq_delay_us(), which counts SysTick cycles at the
 * live core clock.
 *
 * @param us  Delay duration in microseconds
 */
void tiku_clock_arch_delay(unsigned int us) {
    tiku_cpu_ambiq_delay_us(us);
}

/**
 * @brief Return the sub-tick fine counter; this port has none
 *
 * @return 0 always
 */
unsigned short tiku_clock_arch_fine(void)     { return 0; }

/**
 * @brief Return the maximum value of the fine counter
 *
 * @return 1 always, a non-zero divisor for callers that normalise the fine
 *         counter
 */
int            tiku_clock_arch_fine_max(void) { return 1; }

/**
 * @brief Report whether the last tick had a clock fault
 *
 * @return 0 always: this port does not detect clock faults
 */
unsigned char  tiku_clock_arch_fault(void)    { return 0; }
