/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_hal.h - Platform-routing header for ADC
 *
 * Routes to the correct architecture-specific ADC header based on the
 * selected platform. This is the single point where the arch ADC header
 * enters the include chain for the platform-independent ADC layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ADC_HAL_H_
#define TIKU_ADC_HAL_H_

#if defined(PLATFORM_MSP430)
#include <arch/msp430/tiku_adc_arch.h>
#elif defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_adc_arch.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_adc_arch.h>
#endif

#endif /* TIKU_ADC_HAL_H_ */
