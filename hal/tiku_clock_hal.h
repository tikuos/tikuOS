/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock_hal.h - system clock functions every port implements.
 *
 * Declares the tick, seconds, delay and clock-fault calls behind
 * kernel/timers/tiku_clock.c; each port defines them in its timer arch file.
 * Includes no platform header.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CLOCK_HAL_H_
#define TIKU_CLOCK_HAL_H_

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @typedef tiku_clock_arch_time_t
 * @brief Architecture-specific clock time type
 *
 * Each port's tiku_timer_arch.h defines it as unsigned long; this fallback,
 * the same type, applies when that header has not been included first.
 */
#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize platform-specific clock hardware
 */
void tiku_clock_arch_init(void);

/**
 * @brief Get current clock time in ticks
 * @return Current tick count
 */
tiku_clock_arch_time_t tiku_clock_arch_time(void);

/**
 * @brief Read the seconds counter
 * @return Value last set (0 at init) plus whole seconds elapsed since
 */
unsigned long tiku_clock_arch_seconds(void);

/**
 * @brief Set the seconds counter
 * @param sec Seconds value to set
 */
void tiku_clock_arch_set_seconds(unsigned long sec);

/**
 * @brief Busy-wait for specified clock ticks
 * @param t Number of ticks to wait
 */
void tiku_clock_arch_wait(tiku_clock_arch_time_t t);

/**
 * @brief CPU delay loop
 * @param i Delay units (platform-specific calibration)
 */
void tiku_clock_arch_delay(unsigned int i);

/**
 * @brief Get fine-grained clock value
 * @return Timer counter value within current tick
 */
unsigned short tiku_clock_arch_fine(void);

/**
 * @brief Get maximum fine clock value
 * @return Maximum fine clock count
 */
int tiku_clock_arch_fine_max(void);

/*---------------------------------------------------------------------------*/
/* CLOCK SOURCE FAULT REPORTING                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clock source fault codes.
 *
 * A code other than NONE means the port could not start the configured
 * low-frequency source and runs the tick from a less accurate one: the tick
 * rate then differs from TIKU_CLOCK_SECOND and every software timer drifts.
 */
enum tiku_clock_arch_fault_code {
  TIKU_CLOCK_ARCH_FAULT_NONE     = 0, /**< Configured source is in use */
  TIKU_CLOCK_ARCH_FAULT_LFXT_VLO = 1, /**< XT1 failed; fell back to VLO */
};

/**
 * @brief Return the current clock-source fault code.
 *
 * Set by tiku_clock_arch_init() when the configured source does not start;
 * it holds until the next init.
 *
 * @return A tiku_clock_arch_fault_code value
 */
unsigned char tiku_clock_arch_fault(void);

#endif /* TIKU_CLOCK_HAL_H_ */
