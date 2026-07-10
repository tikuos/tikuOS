/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_common_hal.h - Platform-routing header for common utilities
 *
 * Routes to the correct architecture-specific common header based
 * on the selected platform. This is the single point where the arch
 * common header enters the include chain.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_COMMON_HAL_H_
#define TIKU_COMMON_HAL_H_

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
#endif

#endif /* TIKU_COMMON_HAL_H_ */
