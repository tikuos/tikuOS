/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_hal.h - Platform-routing header for I2C bus
 *
 * Routes to the correct architecture-specific I2C header based on the
 * selected platform. This is the single point where the arch I2C header
 * enters the include chain for the platform-independent bus layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_I2C_HAL_H_
#define TIKU_I2C_HAL_H_

#if defined(PLATFORM_MSP430)
#include <arch/msp430/tiku_i2c_arch.h>
#elif defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_i2c_arch.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_i2c_arch.h>
#elif defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_i2c_arch.h>
#endif

#endif /* TIKU_I2C_HAL_H_ */
