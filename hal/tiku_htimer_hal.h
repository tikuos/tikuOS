/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_hal.h - per-port hardware timer configuration and contract.
 *
 * Includes the active platform's tiku_htimer_config.h and states what a port
 * provides; the arch prototypes are in kernel/timers/tiku_htimer.h.
 *
 * SPDX-License-Identifier: Apache-2.0
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
#elif defined(PLATFORM_ESP32C5)
#include "arch/esp32c5/tiku_timer_arch.h"
#endif

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/*
 * The htimer module calls three arch functions, declared in tiku_htimer.h:
 * tiku_htimer_arch_init(), tiku_htimer_arch_schedule() and
 * tiku_htimer_arch_now().  The config header defines TIKU_HTIMER_ARCH_SECOND
 * as the hardware tick frequency in Hz.
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
