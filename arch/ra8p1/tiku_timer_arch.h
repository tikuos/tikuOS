/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - RA8P1 kernel clock on SysTick.
 *
 * SysTick is a core exception, so the tick needs no module-stop bit, no NVIC
 * line and no peripheral clock tree -- the least that can go wrong while the
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_TIMER_ARCH_H_
#define TIKU_RA8P1_TIMER_ARCH_H_

#include <stdint.h>

#include <arch/ra8p1/tiku_device_select.h>

/**
 * @brief Monotonic tick count since boot.
 *
 * At 128 Hz a 32-bit counter wraps in about 388 days, so comparisons use the
 * wraparound-safe TIKU_CLOCK_LT / TIKU_CLOCK_GT macros.
 */
#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/** @brief Sub-tick counter type, taken from the SysTick current-value register. */
typedef unsigned int tiku_clock_arch_counter_t;


/**
 * @brief System tick frequency in Hz; must be a power of two.
 *
 * 128 Hz matches every other port, giving a 7.8 ms tick.
 */
#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128
#endif

/** @brief Resolved tick frequency -- use this, not the CONF_ form. */
#define TIKU_CLOCK_ARCH_SECOND  TIKU_CLOCK_ARCH_CONF_SECOND

/**
 * @brief SysTick counts per tick at the boot clock.
 *
 * 8 MHz / 128 = 62500, inside SysTick's 24-bit reload.  At 1 GHz the same
 * tick would want 7.8M counts, which does not fit, so R4 must divide the
 * source or move the tick to ULPT/AGT.
 */
#define TIKU_CLOCK_ARCH_INTERVAL \
    (TIKU_RA8P1_ICLK_BOOT_HZ / TIKU_CLOCK_ARCH_SECOND)
/*
 * The HAL types the fine counter as unsigned short, so the reload it reports
 * against must fit 16 bits.  At 8 MHz / 128 Hz the reload is 62500, which does
 * -- but at any clock above ~8.4 MHz it would not, and the value would wrap
 * silently mid-tick rather than fail.  R4 raises the clock, so this is a
 * link-time tripwire, not a comment.
 */
_Static_assert(TIKU_CLOCK_ARCH_INTERVAL <= 65535U,
               "sub-tick reload exceeds the HAL's 16-bit fine counter; give "
               "tiku_clock_arch_fine() a shift, or divide the tick source");

/*---------------------------------------------------------------------------*/
/* HAL entry points                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Start the kernel tick. */
void tiku_clock_arch_init(void);

/** @brief Ticks since boot. */
tiku_clock_arch_time_t tiku_clock_arch_time(void);

/** @brief Whole seconds since boot, as the tick counts them. */
unsigned long tiku_clock_arch_seconds(void);

/**
 * @brief Report whether SysTick is armed to raise its exception.
 *
 * @return 1 when the tick is enabled and its interrupt unmasked, 0 otherwise
 */
int tiku_ra8p1_clock_arch_running(void);

/** @brief Counts elapsed inside the current tick, for sub-tick timing. */
unsigned short tiku_clock_arch_fine(void);

/** @brief Largest value tiku_clock_arch_fine() can return. */
int tiku_clock_arch_fine_max(void);

/**
 * @brief Busy-wait until the tick counter reaches @p t.
 *
 * @param t  Tick value to wait for
 */
void tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/**
 * @brief Spin for a platform-calibrated number of delay units.
 *
 * @param i  Delay units
 */
void tiku_clock_arch_delay(unsigned int i);

/**
 * @brief Re-arm the tick for a new processor clock.
 *
 * @param iclk_hz  the processor clock the tick should now be computed from
 * @return 0 when the required reload fits SysTick's 24 bits, -1 otherwise
 *         (in which case the tick is left running at its previous rate).
 */
int tiku_ra8p1_clock_arch_retune(unsigned long iclk_hz);

#endif /* TIKU_RA8P1_TIMER_ARCH_H_ */
