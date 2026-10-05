/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_common_hal.h - Platform-routing header for common utilities
 *
 * Routes to the active platform's tiku_cpu_common.h and maps the
 * tiku_common_arch_* calls that kernel/cpu/tiku_common.c makes onto that
 * port's delay, unique-ID and reset-reason functions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_COMMON_HAL_H_
#define TIKU_COMMON_HAL_H_

/**
 * @def tiku_common_arch_delay_ms(ms)
 * @brief Busy-wait @p ms milliseconds.
 */

/**
 * @def tiku_common_arch_delay_us(us)
 * @brief Busy-wait @p us microseconds.
 */

/**
 * @def tiku_common_arch_unique_id(b, l)
 * @brief Copy up to @p l bytes of the chip's unique ID into @p b.
 * @return Number of bytes written
 */

/**
 * @def tiku_common_arch_reset_reason()
 * @brief Cause of the last reset, as an MSP430 SYSRSTIV code.
 *
 * MSP430 returns SYSRSTIV itself; every other port maps its reset flags onto
 * those codes, which /sys/boot decodes.
 */

#if defined(PLATFORM_MSP430)
#include "arch/msp430/tiku_cpu_common.h"

#define tiku_common_arch_delay_ms(ms)   tiku_cpu_msp430_delay_ms(ms)
#define tiku_common_arch_delay_us(us)   tiku_cpu_msp430_delay_us(us)
#define tiku_common_arch_unique_id(b,l) tiku_cpu_msp430_unique_id((b),(l))
#define tiku_common_arch_reset_reason() tiku_cpu_msp430_reset_reason()

#elif defined(PLATFORM_RP2350)
#include "arch/arm-rp2350/tiku_cpu_common.h"

#define tiku_common_arch_delay_ms(ms)   tiku_cpu_rp2350_delay_ms(ms)
#define tiku_common_arch_delay_us(us)   tiku_cpu_rp2350_delay_us(us)
#define tiku_common_arch_unique_id(b,l) tiku_cpu_rp2350_unique_id((b),(l))
#define tiku_common_arch_reset_reason() tiku_cpu_rp2350_reset_reason()

#elif defined(PLATFORM_AMBIQ)
#include "arch/ambiq/tiku_cpu_common.h"

#define tiku_common_arch_delay_ms(ms)   tiku_cpu_ambiq_delay_ms(ms)
#define tiku_common_arch_delay_us(us)   tiku_cpu_ambiq_delay_us(us)
#define tiku_common_arch_unique_id(b,l) tiku_cpu_ambiq_unique_id((b),(l))
#define tiku_common_arch_reset_reason() tiku_cpu_ambiq_reset_reason()

#elif defined(PLATFORM_NORDIC)
#include "arch/nordic/tiku_cpu_common.h"

#define tiku_common_arch_delay_ms(ms)   tiku_cpu_nordic_delay_ms(ms)
#define tiku_common_arch_delay_us(us)   tiku_cpu_nordic_delay_us(us)
#define tiku_common_arch_unique_id(b,l) tiku_cpu_nordic_unique_id((b),(l))
#define tiku_common_arch_reset_reason() tiku_cpu_nordic_reset_reason()

#elif defined(PLATFORM_STM32N6)
#include "arch/stm32n6/tiku_cpu_common.h"

#define tiku_common_arch_delay_ms(ms)   tiku_cpu_stm32n6_delay_ms(ms)
#define tiku_common_arch_delay_us(us)   tiku_cpu_stm32n6_delay_us(us)
#define tiku_common_arch_unique_id(b,l) tiku_cpu_stm32n6_unique_id((b),(l))
#define tiku_common_arch_reset_reason() tiku_cpu_stm32n6_reset_reason()

#elif defined(PLATFORM_RA8P1)
#include "arch/ra8p1/tiku_cpu_common.h"

#define tiku_common_arch_delay_ms(ms)   tiku_cpu_ra8p1_delay_ms(ms)
#define tiku_common_arch_delay_us(us)   tiku_cpu_ra8p1_delay_us(us)
#define tiku_common_arch_unique_id(b,l) tiku_cpu_ra8p1_unique_id((b),(l))
#define tiku_common_arch_reset_reason() tiku_cpu_ra8p1_reset_reason()

#elif defined(PLATFORM_ESP32C61)
#include "arch/esp32c61/tiku_cpu_common.h"

#define tiku_common_arch_delay_ms(ms)   tiku_cpu_esp32c61_delay_ms(ms)
#define tiku_common_arch_delay_us(us)   tiku_cpu_esp32c61_delay_us(us)
#define tiku_common_arch_unique_id(b,l) tiku_cpu_esp32c61_unique_id((b),(l))
#define tiku_common_arch_reset_reason() tiku_cpu_esp32c61_reset_reason()
#endif

#endif /* TIKU_COMMON_HAL_H_ */
