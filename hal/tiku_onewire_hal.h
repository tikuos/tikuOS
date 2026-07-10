/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_hal.h - Platform-routing header for 1-Wire bus
 *
 * Routes to the correct architecture-specific 1-Wire header based on the
 * selected platform.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ONEWIRE_HAL_H_
#define TIKU_ONEWIRE_HAL_H_

#if defined(PLATFORM_MSP430)
#include <arch/msp430/tiku_onewire_arch.h>
#elif defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_onewire_arch.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_onewire_arch.h>
#elif defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_onewire_arch.h>
#endif

#endif /* TIKU_ONEWIRE_HAL_H_ */
