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
 * is R+W+X, so kernel state there needs no unlock per store.  Tests compare
 * MPUSAM against this value.
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
 * re-enables the MPU, keeping MPUSEGIE.  Every MPUCTL0 write carries the
 * password MPUPW (0xA500) in its upper byte.
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
 * Writes MPUSEGB1 and MPUSEGB2 from TIKU_DEVICE_MPU_SEG2_START and
 * TIKU_DEVICE_MPU_SEG3_START (shifted right by 4) and enables the MPU.
 *
 * @note Run before any permission is set: the permissions apply to the
 *       segments these boundaries define.
 */
void tiku_mpu_arch_init_segments(void);

/**
 * @brief Set default NVM protection (TIKU_MPU_DEFAULT_SAM).
 *
 * Every segment is read+execute without write, except segment 3 on parts
 * with HIFRAM, which is R+W+X.  Called by the kernel during MPU init.
 */
void tiku_mpu_arch_set_default_protection(void);

/**
 * @brief Set permissions on a single MPU segment.
 *
 * Updates one segment's three permission bits without touching the others.
 * @p seg 0, 1 and 2 are MPU segments 1, 2 and 3; @p perm is
 * TIKU_MPU_READ/WRITE/EXEC or a combination.
 */
void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Unlock NVM for writing on all segments
 *
 * Adds write permission to all segments. Returns the prior state, which
 * tiku_mpu_arch_lock_nvm() restores.
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
 * One bit per segment, low bit first, as the SYSNMI ISR latched them from
 * MPUCTL1: a set bit means a write was attempted on that segment while it
 * lacked write permission.
 *
 * @return Violation flags (bits [2:0] meaningful)
 */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/**
 * @brief Clear all MPU violation flags
 *
 * Clears the latched flags and MPUCTL1's segment flags, then re-enables
 * the MPU with the violation NMI on.
 */
void tiku_mpu_arch_clear_violation_flags(void);

/**
 * @brief Raise SYSNMI on an MPU violation (MPUSEGIE).
 *
 * The SYSNMI ISR then latches the flags and the system keeps running.  With
 * MPUSEGIE clear and the violation-select bits clear, as the default SAM
 * leaves them, a blocked store is dropped with only an MPUCTL1 flag set.
 */
void tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_MPU_ARCH_H_ */
