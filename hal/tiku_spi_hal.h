/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_hal.h - Platform-routing header for SPI bus
 *
 * Routes to the correct architecture-specific SPI header based on the
 * selected platform. This is the single point where the arch SPI header
 * enters the include chain for the platform-independent bus layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SPI_HAL_H_
#define TIKU_SPI_HAL_H_

#if defined(PLATFORM_MSP430)
#include <arch/msp430/tiku_spi_arch.h>
#elif defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_spi_arch.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_spi_arch.h>
#elif defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_spi_arch.h>
#endif

#endif /* TIKU_SPI_HAL_H_ */
