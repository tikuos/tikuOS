/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_hal.h - platform routing for the SPI bus driver.
 *
 * Includes arch/<platform>/tiku_spi_arch.h for the PLATFORM_* macro the build
 * defines. Code outside arch/ includes the SPI arch header only through this
 * file.
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
#elif defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_spi_arch.h>
#elif defined(PLATFORM_RA8P1)
#include <arch/ra8p1/tiku_spi_arch.h>
#elif defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_spi_arch.h>
#endif

#endif /* TIKU_SPI_HAL_H_ */
