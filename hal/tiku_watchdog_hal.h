/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watchdog_hal.h - Platform-routing header for watchdog timer
 *
 * Routes to the active platform's watchdog header and maps the
 * tiku_watchdog_arch_* calls that kernel/cpu/tiku_watchdog.c makes onto
 * that port's driver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_WATCHDOG_HAL_H_
#define TIKU_WATCHDOG_HAL_H_

#if defined(PLATFORM_MSP430)
#include "arch/msp430/tiku_cpu_watchdog_arch.h"
#elif defined(PLATFORM_RP2350)
#include "arch/arm-rp2350/tiku_cpu_watchdog_arch.h"
#elif defined(PLATFORM_AMBIQ)
#include "arch/ambiq/tiku_cpu_watchdog_arch.h"
#elif defined(PLATFORM_NORDIC)
#include "arch/nordic/tiku_cpu_watchdog_arch.h"
#elif defined(PLATFORM_STM32N6)
#include "arch/stm32n6/tiku_cpu_watchdog_arch.h"
#elif defined(PLATFORM_RA8P1)
#include "arch/ra8p1/tiku_cpu_watchdog_arch.h"
#elif defined(PLATFORM_ESP32C61)
#include "arch/esp32c61/tiku_cpu_watchdog_arch.h"
#elif defined(PLATFORM_ESP32C5)
#include "arch/esp32c5/tiku_watchdog_arch.h"
#endif

/*---------------------------------------------------------------------------*/
/* HAL-NAMED INTERVAL CONSTANTS                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Platform-neutral names for the watchdog interval divider.
 *
 * On MSP430 the value is the WDTIS field of WDTCTL.  Other ports convert the
 * divider to the timeout it gives on a 32 kHz clock (32768 / 32 kHz ~ 1 s).
 */
#if defined(PLATFORM_MSP430)
#define TIKU_WDT_INTERVAL_64        WDTIS__64
#define TIKU_WDT_INTERVAL_512       WDTIS__512
#define TIKU_WDT_INTERVAL_8192      WDTIS__8192
#define TIKU_WDT_INTERVAL_32768     WDTIS__32768
#else
#ifndef TIKU_WDT_INTERVAL_64
#define TIKU_WDT_INTERVAL_64        64U
#endif
#ifndef TIKU_WDT_INTERVAL_512
#define TIKU_WDT_INTERVAL_512       512U
#endif
#ifndef TIKU_WDT_INTERVAL_8192
#define TIKU_WDT_INTERVAL_8192      8192U
#endif
#ifndef TIKU_WDT_INTERVAL_32768
#define TIKU_WDT_INTERVAL_32768     32768U
#endif
#endif

/** @brief Default interval: the 32768 divider, about 1 s. */
#define TIKU_WDT_INTERVAL_DEFAULT   TIKU_WDT_INTERVAL_32768

/*
 * Timeout names: the timeout each divider gives from a 32 kHz clock.
 *
 *   TIKU_WDT_TIMEOUT_2MS    ~  1.95 ms  (divider /64)
 *   TIKU_WDT_TIMEOUT_16MS   ~ 15.6  ms  (divider /512)
 *   TIKU_WDT_TIMEOUT_250MS  ~ 250   ms  (divider /8192)
 *   TIKU_WDT_TIMEOUT_1000MS ~ 1000  ms  (divider /32768)
 *
 * TIKU_WDT_TIMEOUT_* name a timeout; TIKU_WDT_INTERVAL_* name the divider,
 * which MSP430 tests compare with WDTCTL bit patterns.
 */
#define TIKU_WDT_TIMEOUT_2MS        TIKU_WDT_INTERVAL_64
#define TIKU_WDT_TIMEOUT_16MS       TIKU_WDT_INTERVAL_512
#define TIKU_WDT_TIMEOUT_250MS      TIKU_WDT_INTERVAL_8192
#define TIKU_WDT_TIMEOUT_1000MS     TIKU_WDT_INTERVAL_32768

/*---------------------------------------------------------------------------*/
/* HAL-TO-ARCH MAPPING                                                       */
/*---------------------------------------------------------------------------*/

/*
 * Each port maps the calls kernel/cpu/tiku_watchdog.c makes onto its driver:
 *
 *   tiku_watchdog_arch_on(src, isel)   start in reset mode
 *   tiku_watchdog_arch_off()           stop
 *   tiku_watchdog_arch_kick()          restart the countdown
 *   tiku_watchdog_arch_pause()         hold the counter
 *   tiku_watchdog_arch_resume(kick)    release it, clearing the count first
 *                                      when kick is non-zero
 *   tiku_watchdog_arch_config(mode, src, isel, held, kick)
 *                                      full setup, where interval mode exists
 */

/**
 * @def TIKU_WATCHDOG_INTERVAL_SUPPORTED
 * @brief 1 where the watchdog can also run as an interval timer (MSP430);
 *        there the kernel sets it up with tiku_watchdog_arch_config().
 */

#if defined(PLATFORM_MSP430)
#define TIKU_WATCHDOG_INTERVAL_SUPPORTED 1
#define tiku_watchdog_arch_config(mode, src, isel, held, kick) \
    tiku_cpu_msp430_watchdog_config_arch((mode), (src), (isel), \
                                         (held), (kick))
#define tiku_watchdog_arch_on(src, isel) \
    tiku_cpu_msp430_watchdog_on_arch((src), (isel))
#define tiku_watchdog_arch_off() \
    tiku_cpu_msp430_watchdog_off_arch()
#define tiku_watchdog_arch_kick() \
    tiku_cpu_msp430_watchdog_kick_arch()
#define tiku_watchdog_arch_pause() \
    tiku_cpu_msp430_watchdog_pause_arch()
#define tiku_watchdog_arch_resume(kick) \
    tiku_cpu_msp430_watchdog_resume_arch(kick)
#elif defined(PLATFORM_RP2350)
#define TIKU_WATCHDOG_INTERVAL_SUPPORTED 0
#define tiku_watchdog_arch_on(src, isel) \
    tiku_cpu_rp2350_watchdog_on_arch((src), (isel))
#define tiku_watchdog_arch_off() \
    tiku_cpu_rp2350_watchdog_off_arch()
#define tiku_watchdog_arch_kick() \
    tiku_cpu_rp2350_watchdog_kick_arch()
#define tiku_watchdog_arch_pause() \
    tiku_cpu_rp2350_watchdog_pause_arch()
#define tiku_watchdog_arch_resume(kick) \
    tiku_cpu_rp2350_watchdog_resume_arch(kick)
#elif defined(PLATFORM_AMBIQ)
#define TIKU_WATCHDOG_INTERVAL_SUPPORTED 0
#define tiku_watchdog_arch_on(src, isel) \
    tiku_cpu_ambiq_watchdog_on_arch((src), (isel))
#define tiku_watchdog_arch_off() \
    tiku_cpu_ambiq_watchdog_off_arch()
#define tiku_watchdog_arch_kick() \
    tiku_cpu_ambiq_watchdog_kick_arch()
#define tiku_watchdog_arch_pause() \
    tiku_cpu_ambiq_watchdog_pause_arch()
#define tiku_watchdog_arch_resume(kick) \
    tiku_cpu_ambiq_watchdog_resume_arch(kick)
#elif defined(PLATFORM_NORDIC)
#define TIKU_WATCHDOG_INTERVAL_SUPPORTED 0
#define tiku_watchdog_arch_on(src, isel) \
    tiku_cpu_nordic_watchdog_on_arch((src), (isel))
#define tiku_watchdog_arch_off() \
    tiku_cpu_nordic_watchdog_off_arch()
#define tiku_watchdog_arch_kick() \
    tiku_cpu_nordic_watchdog_kick_arch()
#define tiku_watchdog_arch_pause() \
    tiku_cpu_nordic_watchdog_pause_arch()
#define tiku_watchdog_arch_resume(kick) \
    tiku_cpu_nordic_watchdog_resume_arch(kick)
#elif defined(PLATFORM_STM32N6)
#define TIKU_WATCHDOG_INTERVAL_SUPPORTED 0
#define tiku_watchdog_arch_on(src, isel) \
    tiku_cpu_stm32n6_watchdog_on_arch((src), (isel))
#define tiku_watchdog_arch_off() \
    tiku_cpu_stm32n6_watchdog_off_arch()
#define tiku_watchdog_arch_kick() \
    tiku_cpu_stm32n6_watchdog_kick_arch()
#define tiku_watchdog_arch_pause() \
    tiku_cpu_stm32n6_watchdog_pause_arch()
#define tiku_watchdog_arch_resume(kick) \
    tiku_cpu_stm32n6_watchdog_resume_arch(kick)
#elif defined(PLATFORM_RA8P1)
/* The IWDT cannot interrupt on timeout; this port has no interval mode. */
#define TIKU_WATCHDOG_INTERVAL_SUPPORTED 0
#define tiku_watchdog_arch_on(src, isel) \
    tiku_cpu_ra8p1_watchdog_on_arch((src), (isel))
#define tiku_watchdog_arch_off() \
    tiku_cpu_ra8p1_watchdog_off_arch()
#define tiku_watchdog_arch_kick() \
    tiku_cpu_ra8p1_watchdog_kick_arch()
#define tiku_watchdog_arch_pause() \
    tiku_cpu_ra8p1_watchdog_pause_arch()
#define tiku_watchdog_arch_resume(kick) \
    tiku_cpu_ra8p1_watchdog_resume_arch(kick)
#elif defined(PLATFORM_ESP32C61)
/* The one watchdog stage resets the system; this port has no interval
 * mode. */
#define TIKU_WATCHDOG_INTERVAL_SUPPORTED 0
#define tiku_watchdog_arch_on(src, isel) \
    tiku_cpu_esp32c61_watchdog_on_arch((src), (isel))
#define tiku_watchdog_arch_off() \
    tiku_cpu_esp32c61_watchdog_off_arch()
#define tiku_watchdog_arch_kick() \
    tiku_cpu_esp32c61_watchdog_kick_arch()
#define tiku_watchdog_arch_pause() \
    tiku_cpu_esp32c61_watchdog_pause_arch()
#define tiku_watchdog_arch_resume(kick) \
    tiku_cpu_esp32c61_watchdog_resume_arch(kick)
#endif

#endif /* TIKU_WATCHDOG_HAL_H_ */
