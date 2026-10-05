/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - RA8P1 kernel clock on SysTick.
 *
 * SysTick is a core exception: the tick needs no module-stop bit, no ICU link
 * and no peripheral clock.  It counts the processor clock, and
 * tiku_ra8p1_clock_arch_retune() re-arms it when that clock changes.
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
 * At 128 Hz a 32-bit counter wraps in about 388 days; compare ticks with the
 * wraparound-safe TIKU_CLOCK_LT and TIKU_CLOCK_DIFF (tiku_clock.h).
 */
#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/** @brief Sub-tick counter type, from the SysTick current-value register. */
typedef unsigned int tiku_clock_arch_counter_t;


/**
 * @brief System tick frequency in Hz; must be a power of two.
 *
 * 128 Hz gives a 7.8 ms tick.
 */
#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128
#endif

/** @brief Resolved tick frequency -- use this, not the CONF_ form. */
#define TIKU_CLOCK_ARCH_SECOND  TIKU_CLOCK_ARCH_CONF_SECOND

/**
 * @brief SysTick counts per tick at the boot clock.
 *
 * 8 MHz / 128 = 62500, inside SysTick's 24-bit reload.
 * tiku_ra8p1_clock_arch_retune() reprograms the reload when the clock tree
 * moves; at 1 GHz it is 7812500, still inside the 24 bits.
 */
#define TIKU_CLOCK_ARCH_INTERVAL \
    (TIKU_RA8P1_ICLK_BOOT_HZ / TIKU_CLOCK_ARCH_SECOND)
/* Checks the boot reload only.  A faster clock's reload exceeds 16 bits,
 * which tiku_clock_arch_fine() shifts away; SysTick's 24-bit reload is a
 * hard limit. */
_Static_assert(TIKU_CLOCK_ARCH_INTERVAL <= 0x00FFFFFFUL,
               "boot tick reload exceeds SysTick's 24 bits");

/*---------------------------------------------------------------------------*/
/* HAL ENTRY POINTS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Start the kernel tick from the live processor clock. */
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

/**
 * @brief Effective resolution of tiku_clock_arch_fine(), in counts per second.
 *
 * @return Sub-tick counts per second after the range shift
 */
uint32_t tiku_ra8p1_clock_arch_fine_hz(void);

/** @brief Counts elapsed inside the current tick, for sub-tick timing. */
unsigned short tiku_clock_arch_fine(void);

/** @brief Largest value tiku_clock_arch_fine() can return. */
int tiku_clock_arch_fine_max(void);

/**
 * @brief Wait @p t ticks.
 *
 * Sleeps in WFI while the tick runs; with interrupts masked or the tick
 * stopped, it spins a calibrated delay instead.
 *
 * @param t  Ticks to wait
 */
void tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/**
 * @brief Spin @p i turns of a one-nop loop; the time per turn is not
 *        calibrated.
 *
 * @param i  Loop turns
 */
void tiku_clock_arch_delay(unsigned int i);

/**
 * @brief Re-arm the tick for a new processor clock.
 *
 * @param iclk_hz  the processor clock the tick should now be computed from
 * @return 0 when the required reload fits SysTick's 24 bits, -1 otherwise,
 *         with the reload left unchanged
 */
int tiku_ra8p1_clock_arch_retune(unsigned long iclk_hz);

/**
 * @brief Re-derive the htimer's counts-per-microsecond from PCLKD.
 *
 * An alarm already programmed keeps its GPT count, so its deadline shifts
 * with the clock, as the tick's does.
 *
 * @note Call on every rung change: PCLKD is 240 MHz at the 240 and 480
 *       rungs and 250 MHz at 1000.
 */
void tiku_ra8p1_htimer_arch_retune(void);

#endif /* TIKU_RA8P1_TIMER_ARCH_H_ */
