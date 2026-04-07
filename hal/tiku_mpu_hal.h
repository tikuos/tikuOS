/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
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
/* LOW-LEVEL REGISTER ACCESS (diagnostic / test use)                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the current segment-access-mode register
 *
 * Returns the raw value of the hardware register that holds per-segment
 * read/write/execute permission bits (MPUSAM on MSP430). Primarily used
 * for diagnostic inspection and testing; the kernel should use the
 * higher-level arch functions instead.
 *
 * @return Current permission register value
 */
uint16_t tiku_mpu_arch_get_sam(void);

/**
 * @brief Write a new value to the segment-access-mode register
 *
 * Handles any password/unlock sequence required by the hardware,
 * writes the new permission bits, and re-enables the MPU. Primarily
 * used internally by arch-level functions; the kernel should use the
 * higher-level arch functions instead.
 *
 * @param sam  New permission register value
 */
void tiku_mpu_arch_set_sam(uint16_t sam);

/**
 * @brief Read the current MPU control register
 *
 * Returns the raw value of the MPU control register (MPUCTL0 on MSP430).
 * Used to check whether the MPU is enabled.
 *
 * @return Current control register value
 */
uint16_t tiku_mpu_arch_get_ctl(void);

/*---------------------------------------------------------------------------*/
/* INTERRUPT CONTROL                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Disable interrupts (platform-specific)
 *
 * Called by the kernel MPU layer to enter a critical section before
 * unlocking NVM for a scoped write.
 */
void tiku_mpu_arch_disable_irq(void);

/**
 * @brief Enable interrupts (platform-specific)
 *
 * Called by the kernel MPU layer to exit the critical section after
 * a scoped write relocks NVM.
 */
void tiku_mpu_arch_enable_irq(void);

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS (called by kernel)                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure MPU segment boundaries
 *
 * Sets the hardware boundary registers to partition NVM into segments.
 * Must be called before enabling MPU protection so that the permission
 * settings map to meaningful address ranges.
 */
void tiku_mpu_arch_init_segments(void);

/**
 * @brief Set default NVM protection on all segments
 *
 * Configures all MPU segments to read+execute with no write permission.
 * The specific register encoding is handled entirely by the arch layer.
 */
void tiku_mpu_arch_set_default_protection(void);

/**
 * @brief Set permissions on a single MPU segment
 *
 * Updates the permission bits for one segment without affecting others.
 * The seg and perm values correspond to the platform-independent
 * tiku_mpu_seg_t and tiku_mpu_perm_t enums (passed as uint8_t).
 *
 * @param seg    Segment number (0-2)
 * @param perm   Permission flags (TIKU_MPU_READ/WRITE/EXEC or combinations)
 */
void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Unlock NVM for writing on all segments
 *
 * Adds write permission to every segment. Returns an opaque saved state
 * that must be passed to tiku_mpu_arch_lock_nvm() to restore the
 * original protection.
 *
 * @return Previous protection state (opaque to the kernel)
 */
uint16_t tiku_mpu_arch_unlock_nvm(void);

/**
 * @brief Restore NVM protection to a previously saved state
 *
 * @param saved_state  Value returned by a prior tiku_mpu_arch_unlock_nvm()
 */
void tiku_mpu_arch_lock_nvm(uint16_t saved_state);

/**
 * @brief Read violation flags from the MPU
 *
 * Returns segment violation flags. Bit 0 = segment 1 violation,
 * bit 1 = segment 2, bit 2 = segment 3. A set bit means a write
 * was attempted while that segment lacked write permission.
 *
 * @return Violation flags (bits [2:0] meaningful)
 */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/**
 * @brief Clear all MPU violation flags
 *
 * Resets all segment violation flags so the next violation can be
 * detected cleanly.
 */
void tiku_mpu_arch_clear_violation_flags(void);

/**
 * @brief Enable NMI on MPU violation (instead of device reset)
 *
 * On platforms where the default MPU violation response is a reset,
 * this function switches to a non-maskable interrupt instead, allowing
 * software to detect and handle violations without losing state.
 */
void tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_MPU_HAL_H_ */
