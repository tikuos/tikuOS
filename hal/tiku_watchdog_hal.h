/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watchdog_hal.h - Platform-routing header for watchdog timer
 *
 * Routes to the correct architecture-specific watchdog header based
 * on the selected platform. This is the single point where the arch
 * watchdog header enters the include chain.
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
#endif

/*---------------------------------------------------------------------------*/
/* HAL-NAMED INTERVAL CONSTANTS                                              */
/*---------------------------------------------------------------------------*/

/*
 * Platform-neutral aliases for the watchdog interval divider. On MSP430
 * the value plugs straight into WDTCTL. On other platforms the arch
 * converts the divider to a microsecond timeout (e.g. 32768 / 32 kHz
 * ≈ 1 s on MSP430; the RP2350 arch reproduces the same wall-clock
 * effect against the 1 us tick clock).
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

#define TIKU_WDT_INTERVAL_DEFAULT   TIKU_WDT_INTERVAL_32768

/*
 * Semantic timeout aliases. The MSP430 divider model only naturally
 * yields four time points when paired with a 32 kHz ACLK, so we
 * surface those four with names that read like wall-clock timeouts:
 *
 *   TIKU_WDT_TIMEOUT_2MS    ~  1.95 ms  (divider /64)
 *   TIKU_WDT_TIMEOUT_16MS   ~ 15.6  ms  (divider /512)
 *   TIKU_WDT_TIMEOUT_250MS  ~ 250   ms  (divider /8192)
 *   TIKU_WDT_TIMEOUT_1000MS ~ 1000  ms  (divider /32768)
 *
 * Use these in portable code; reach for the underlying
 * TIKU_WDT_INTERVAL_* only if you genuinely care about the divider
 * (e.g. MSP430-specific tests asserting WDTCTL bit patterns).
 */
#define TIKU_WDT_TIMEOUT_2MS        TIKU_WDT_INTERVAL_64
#define TIKU_WDT_TIMEOUT_16MS       TIKU_WDT_INTERVAL_512
#define TIKU_WDT_TIMEOUT_250MS      TIKU_WDT_INTERVAL_8192
#define TIKU_WDT_TIMEOUT_1000MS     TIKU_WDT_INTERVAL_32768

/*---------------------------------------------------------------------------*/
/* HAL-to-arch mapping macros                                                */
/*---------------------------------------------------------------------------*/

#if defined(PLATFORM_MSP430)
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
#endif

#endif /* TIKU_WATCHDOG_HAL_H_ */
