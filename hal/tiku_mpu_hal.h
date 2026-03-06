/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_hal.h - Platform-routing header for MPU (Memory Protection Unit)
 *
 * Routes to the correct architecture-specific MPU header based on the
 * selected platform. Provides portable fallback stubs when no platform
 * is selected (e.g. host-mode testing).
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MPU_HAL_H_
#define TIKU_MPU_HAL_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* PLATFORM ROUTING                                                          */
/*---------------------------------------------------------------------------*/

#ifdef PLATFORM_MSP430
#include "arch/msp430/tiku_mpu_arch.h"
#endif

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the current segment-access-mode register
 *
 * Returns the raw value of the hardware register that holds per-segment
 * read/write/execute permission bits (MPUSAM on MSP430).
 *
 * @return Current permission register value
 */
uint16_t tiku_mpu_arch_get_sam(void);

/**
 * @brief Write a new value to the segment-access-mode register
 *
 * Handles any password/unlock sequence required by the hardware,
 * writes the new permission bits, and re-enables the MPU.
 *
 * @param sam  New permission register value
 */
void tiku_mpu_arch_set_sam(uint16_t sam);

/**
 * @brief Read the current MPU control register
 *
 * Returns the raw value of the MPU control register (MPUCTL0 on MSP430).
 * Used by the kernel to check whether the MPU is enabled.
 *
 * @return Current control register value
 */
uint16_t tiku_mpu_arch_get_ctl(void);

/**
 * @brief Disable interrupts (platform-specific)
 *
 * Called by the kernel MPU layer to enter a critical section before
 * unlocking FRAM for a scoped write.
 */
void tiku_mpu_arch_disable_irq(void);

/**
 * @brief Enable interrupts (platform-specific)
 *
 * Called by the kernel MPU layer to exit the critical section after
 * a scoped write relocks FRAM.
 */
void tiku_mpu_arch_enable_irq(void);

#endif /* TIKU_MPU_HAL_H_ */
