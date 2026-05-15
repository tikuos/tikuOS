/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_htimer_config.h - STM32F411RE htimer configuration
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_HTIMER_CONFIG_H_
#define TIKU_STM32F411_HTIMER_CONFIG_H_

#include <stdint.h>

#ifndef TIKU_HTIMER_CLOCK_T_DEFINED
typedef uint32_t tiku_htimer_clock_t;
#define TIKU_HTIMER_CLOCK_T_DEFINED
#endif

#define TIKU_HTIMER_ARCH_SECOND  1000000UL
#define TIKU_HTIMER_CLOCK_DIFF(a, b) ((int32_t)((a) - (b)))

#endif /* TIKU_STM32F411_HTIMER_CONFIG_H_ */
