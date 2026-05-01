/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
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
#include "tiku.h"

/*---------------------------------------------------------------------------*/
/* DEFAULT SAM AT BOOT                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Expected MPUSAM value after tiku_mpu_init() / tiku_mem_init().
 *
 * Parts without HIFRAM (FR5969, FR4133, ...): every segment is R+X
 * with no W bit set, i.e. 0x0555.
 *
 * Parts with HIFRAM (FR6989, FR5994, ...): segment 3 covers HIFRAM
 * (0x10000+) and must be writable so kernel mutable state placed
 * there can be updated without an explicit unlock around every
 * store. Segments 1-2 stay R+X. The resulting SAM is 0x0755.
 *
 * Tests and assertions reach for this constant rather than hard-coding
 * 0x0555 — that hard-coding is correct on FR5969 but wrong on FR6989,
 * and was the source of the FR6989 mpu / memory-edge regressions.
 */
#if defined(TIKU_DEVICE_HAS_HIFRAM) && TIKU_DEVICE_HAS_HIFRAM
#define TIKU_MPU_DEFAULT_SAM    0x0755U
#else
#define TIKU_MPU_DEFAULT_SAM    0x0555U
#endif

/*---------------------------------------------------------------------------*/
/* LOW-LEVEL REGISTER ACCESS                                                 */
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

/*---------------------------------------------------------------------------*/
/* INTERRUPT CONTROL                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Disable interrupts via __disable_interrupt() intrinsic
 */
void tiku_mpu_arch_disable_irq(void);

/**
 * @brief Enable interrupts via __enable_interrupt() intrinsic
 */
void tiku_mpu_arch_enable_irq(void);

/*---------------------------------------------------------------------------*/
/* HIGHER-LEVEL ARCH FUNCTIONS (called by kernel)                            */
/*---------------------------------------------------------------------------*/

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
 * @brief Set default NVM protection: R+X (no write) on all segments
 *
 * Configures the SAM register so all three segments are read+execute
 * with no write permission. Called by the kernel during MPU init.
 */
void tiku_mpu_arch_set_default_protection(void);

/**
 * @brief Set permissions on a single MPU segment
 *
 * Updates the permission bits for one segment without affecting the
 * other segments. The seg and perm values use the platform-independent
 * TIKU_MPU_SEG and TIKU_MPU_PERM enums (passed as uint8_t to avoid
 * circular header dependencies).
 *
 * @param seg    Segment number (0-2)
 * @param perm   Permission flags (TIKU_MPU_READ/WRITE/EXEC or combinations)
 */
void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Unlock NVM for writing on all segments
 *
 * Adds write permission to all segments. Returns an opaque saved state
 * that must be passed to tiku_mpu_arch_lock_nvm() to restore protection.
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
 * @brief Read violation flags
 *
 * Returns per-segment violation flags. Each bit corresponds to one
 * segment: bit 0 = segment 1, bit 1 = segment 2, bit 2 = segment 3.
 * A set bit means a write was attempted on that segment while it
 * lacked write permission.
 *
 * @return Violation flags (bits [2:0] meaningful)
 */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/**
 * @brief Clear all MPU violation flags
 *
 * Resets all segment violation flags so subsequent violations can be
 * detected cleanly.
 */
void tiku_mpu_arch_clear_violation_flags(void);

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
