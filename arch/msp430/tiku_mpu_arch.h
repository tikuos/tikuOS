/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - MSP430 MPU architecture declarations
 *
 * Declares the arch-level MPU functions for the MSP430FR series.
 * Included indirectly via hal/tiku_mpu_hal.h when PLATFORM_MSP430
 * is defined.
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

#ifndef TIKU_MPU_ARCH_H_
#define TIKU_MPU_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* FUNCTION DECLARATIONS                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read MPUSAM register
 *
 * Returns the current segment-access-mode register value. On MSP430,
 * MPUSAM is a 16-bit register where each 4-bit nybble controls one
 * segment's read/write/execute permissions.
 *
 * @return Current MPUSAM value
 */
uint16_t tiku_mpu_arch_get_sam(void);

/**
 * @brief Write MPUSAM register with password unlock
 *
 * Unlocks MPU config via MPUPW, writes the new MPUSAM value, and
 * re-enables the MPU. The password (0xA500) is required by hardware
 * to prevent accidental modification by wild pointer writes.
 *
 * @param sam  New MPUSAM value
 */
void tiku_mpu_arch_set_sam(uint16_t sam);

/**
 * @brief Read MPUCTL0 register
 *
 * Returns the current MPU control register (lower byte only is
 * meaningful; upper byte is the password field on writes).
 *
 * @return Current MPUCTL0 value
 */
uint16_t tiku_mpu_arch_get_ctl(void);

/**
 * @brief Disable interrupts via __disable_interrupt() intrinsic
 */
void tiku_mpu_arch_disable_irq(void);

/**
 * @brief Enable interrupts via __enable_interrupt() intrinsic
 */
void tiku_mpu_arch_enable_irq(void);

/**
 * @brief Read MPUCTL1 violation flags
 *
 * Returns the current segment violation flags. Each bit corresponds
 * to one segment: bit 0 = segment 1, bit 1 = segment 2, bit 2 = segment 3.
 * A set bit means a write was attempted on that segment while it lacked
 * write permission.
 *
 * @return Current MPUCTL1 value (violation flags in bits [2:0])
 */
uint16_t tiku_mpu_arch_get_ctl1(void);

/**
 * @brief Clear MPUCTL1 violation flags
 *
 * Clears all segment violation flags (MPUSEG1IFG, MPUSEG2IFG,
 * MPUSEG3IFG) so subsequent violations can be detected cleanly.
 */
void tiku_mpu_arch_clear_ctl1(void);

/**
 * @brief Configure MPU segment boundaries
 *
 * Sets MPUSEGB1 and MPUSEGB2 to partition FRAM into three segments.
 * Boundary addresses are taken from device-level constants
 * (TIKU_DEVICE_MPU_SEG2_START, TIKU_DEVICE_MPU_SEG3_START) and
 * right-shifted by 4 before writing to the boundary registers.
 *
 * Must be called before enabling MPU protection so that the SAM
 * permissions map to meaningful address ranges.
 */
void tiku_mpu_arch_init_segments(void);

/**
 * @brief Enable NMI on MPU violation instead of PUC reset
 *
 * Sets the MPUSEGIE bit in MPUCTL0. When a violation occurs the
 * CPU vectors to the SYSNMI handler rather than performing a full
 * power-up clear (reset). This allows violation detection without
 * losing system state.
 */
void tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_MPU_ARCH_H_ */
