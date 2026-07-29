/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_hal.h - Hardware abstraction layer interface for hardware timers
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file tiku_htimer_hal.h
 * @brief Platform-agnostic hardware timer interface.
 *
 * Declares what each platform must provide for the htimer subsystem, with no
 * platform headers included.  The arch file supplies the timer control.
 */

#ifndef TIKU_HTIMER_HAL_H_
#define TIKU_HTIMER_HAL_H_

#if defined(PLATFORM_MSP430)
#include "arch/msp430/tiku_htimer_config.h"
#elif defined(PLATFORM_RP2350)
#include "arch/arm-rp2350/tiku_htimer_config.h"
#elif defined(PLATFORM_AMBIQ)
#include "arch/ambiq/tiku_htimer_config.h"
#elif defined(PLATFORM_NORDIC)
#include "arch/nordic/tiku_htimer_config.h"
#endif

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/*
 * The htimer kernel module requires three arch functions -- _init(), _schedule()
 * and _now() -- declared in tiku_htimer.h.  The platform must also define
 * TIKU_HTIMER_ARCH_SECOND as the hardware tick frequency.
 */

/*---------------------------------------------------------------------------*/
/* PLATFORM ISR CONTRACT                                                     */
/*---------------------------------------------------------------------------*/

/*
 * The platform timer ISR must call tiku_htimer_run_next() when the compare-match
 * interrupt fires; that dispatches the pending callback and reschedules.
 */

#endif /* TIKU_HTIMER_HAL_H_ */
