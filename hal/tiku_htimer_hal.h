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
 * Routes to the active platform's htimer config header and states the arch
 * contract; the arch prototypes are in kernel/timers/tiku_htimer.h.
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
#elif defined(PLATFORM_STM32N6)
#include "arch/stm32n6/tiku_htimer_config.h"
#elif defined(PLATFORM_RA8P1)
#include "arch/ra8p1/tiku_htimer_config.h"
#elif defined(PLATFORM_ESP32C61)
#include "arch/esp32c61/tiku_htimer_config.h"
#endif

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/*
 * The htimer kernel module requires three arch functions -- _init(),
 * _schedule() and _now() -- declared in tiku_htimer.h.  The platform must
 * also define TIKU_HTIMER_ARCH_SECOND as the hardware tick frequency.
 */

/*---------------------------------------------------------------------------*/
/* PLATFORM ISR CONTRACT                                                     */
/*---------------------------------------------------------------------------*/

/*
 * The platform timer ISR must call tiku_htimer_run_next() when the
 * compare-match interrupt fires; that dispatches the pending callback, which
 * may re-arm the timer.
 */

#endif /* TIKU_HTIMER_HAL_H_ */
