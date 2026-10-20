/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crypto_hal.h - platform routing for the hardware crypto engines.
 *
 * Includes the port's tiku_crypto_arch.h and defines TIKU_CRYPTO_HW to 1 on
 * the builds that carry one: every Nordic port and the FireBeetle 2 ESP32-P4.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CRYPTO_HAL_H_
#define TIKU_CRYPTO_HAL_H_

#if defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_crypto_arch.h>
#define TIKU_CRYPTO_HW 1
#elif defined(PLATFORM_ESP32P4) && defined(TIKU_BOARD_FIREBEETLE2_ESP32P4)
#include <arch/esp32p4/tiku_crypto_arch.h>
#define TIKU_CRYPTO_HW 1
#else
#define TIKU_CRYPTO_HW 0
#endif

#endif /* TIKU_CRYPTO_HAL_H_ */
