/*
 * Tiku Operating System v0.06
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
 * @brief Expected access-mask value after MPU and memory init.
 *
 * Without HIFRAM every segment is R+X.  With it, segment 3 covers HIFRAM and
 * must be writable so kernel state there needs no unlock per store.  Tests
 * assert against this rather than a literal, which is what broke on FR6989.
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
 * @brief Configure MPU segment boundaries.
 *
 * Partitions FRAM into three segments from the device-level boundary constants.
 * Must run before protection is enabled, so the permission bits map to
 * meaningful address ranges.
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
 * @brief Set permissions on a single MPU segment.
 *
 * Updates one segment's bits without touching the others, taking the
 * platform-independent segment and permission enums as plain integers.
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
 * @brief Read violation flags.
 *
 * One bit per segment, low bit first; a set bit means a write was attempted on
 * that segment while it lacked write permission.
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
 * @brief Enable NMI on MPU violation instead of a PUC reset.
 *
 * Sets the segment-interrupt-enable bit, so a violation vectors to the NMI
 * handler rather than performing a full power-up clear -- detection without
 * losing system state.
 */
void tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_MPU_ARCH_H_ */
