/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - Ambiq MPU driver interface.
 *
 * Apollo510 (ARMv8-M MPU) and Apollo4 Lite and Plus (PMSAv7) program a W^X
 * layout with a stack-overflow guard; .uninit is read-only outside NVM unlock
 * windows.  SAM and MPUCTL0 are MSP430-style copies kept for portable tests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_MPU_ARCH_H_
#define TIKU_AMBIQ_MPU_ARCH_H_

#include <stdint.h>
#include "tiku.h"

/**
 * @brief Default SAM (Segment Access Mask) value for this platform.
 *
 * Segments 0-2 read + execute, no write: the value RP2350 and the MSP430
 * parts without HIFRAM use, so portable tests that compare against
 * TIKU_MPU_DEFAULT_SAM behave identically across arch ports.
 */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

/**
 * @brief Read the current Segment Access Mask from the arch layer.
 *
 * The SAM encodes per-segment read/write/execute permissions for the generic
 * mem layer.  On Ambiq it is a software copy that no MPU region follows,
 * except the .uninit region tiku_mpu_arch_lock_nvm() sets from its write bits.
 *
 * @return Current SAM value.
 */
uint16_t tiku_mpu_arch_get_sam(void);

/**
 * @brief Write a new Segment Access Mask to the arch layer.
 *
 * Stores the SAM and the MSP430-style MPUCTL0 password write; no MPU region
 * changes.
 *
 * @param sam  New SAM value to apply.
 */
void     tiku_mpu_arch_set_sam(uint16_t sam);

/**
 * @brief Read the software MPUCTL0 copy (MSP430 layout).
 *
 * 0xA501 (password | enable) after a SAM write; enable_violation_nmi() ORs in
 * 0x0010 (SEGIE) until the next SAM write.  Not the ARMv8-M MPU_CTRL.
 *
 * @return Current MPUCTL0 copy.
 */
uint16_t tiku_mpu_arch_get_ctl(void);

/**
 * @brief Disable all interrupts (PRIMASK).
 *
 * A plain mask with no nesting count: tiku_mpu_arch_enable_irq() unmasks
 * whatever the state was before.
 */
void     tiku_mpu_arch_disable_irq(void);

/**
 * @brief Enable all interrupts (PRIMASK).
 *
 * The closing half of tiku_mpu_arch_disable_irq().
 */
void     tiku_mpu_arch_enable_irq(void);

/**
 * @brief Program all MPU regions for the part's memory map.
 *
 * Code RX, .uninit RO + XN, other data RW + XN, and a 4 KB guard below the
 * stack (region tables in the .c files); enables the MPU with PRIVDEFENA and
 * MemManage at priority 0.  A cold boot zeroes the .mpu_diag fault record.
 *
 * @note tiku_mpu_init() calls it at boot and on each later tiku_mem_init();
 *       a warm reset keeps the fault record.  HFNMIENA stays off, except on
 *       an Apollo510 build with TIKU_MPU_HFNMI_ENFORCE=1.
 */
void     tiku_mpu_arch_init_segments(void);

/**
 * @brief Set the SAM to TIKU_MPU_DEFAULT_SAM.
 *
 * Bookkeeping only; no MPU region changes.  Called by tiku_mpu_init().
 */
void     tiku_mpu_arch_set_default_protection(void);

/**
 * @brief Set the access permissions for one SAM segment.
 *
 * Bookkeeping only: updates the 3-bit field of @p seg in the SAM copy
 * through tiku_mpu_arch_set_sam().
 *
 * @param seg   Segment index (0-based; 4 bits per segment in the SAM).
 * @param perm  Permission flags (combination of TIKU_MPU_* values); bits
 *              above bit 2 are ignored.
 */
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Unlock NVM for writing and return the previous SAM state.
 *
 * Makes the .uninit MPU region RW and sets the SAM write bits.  The caller must
 * restore the state via tiku_mpu_arch_lock_nvm() as soon as the write is done.
 *
 * @return SAM value before unlocking (pass to tiku_mpu_arch_lock_nvm()).
 */
uint16_t tiku_mpu_arch_unlock_nvm(void);

/**
 * @brief Restore MPU NVM protection to the saved state.
 *
 * Writes @p saved_state back through tiku_mpu_arch_set_sam().  The .uninit
 * region returns to RO unless @p saved_state has write bits set, which is the
 * case for a lock nested inside an outer window.  Commits nothing to MRAM.
 *
 * @param saved_state  Value returned by a prior tiku_mpu_arch_unlock_nvm().
 */
void     tiku_mpu_arch_lock_nvm(uint16_t saved_state);

/**
 * @brief Read MPU violation (fault) flags.
 *
 * Returns the MMFSR bits the MemManage handler ORs into the .mpu_diag record
 * before it resets the part.  The record survives a warm reset, so the flags
 * of earlier faults read back until a clear or a cold boot.
 *
 * @return MMFSR bits ORed together (bit 0 IACCVIOL, bit 1 DACCVIOL, ...).
 */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/**
 * @brief Clear all MPU violation flags.
 *
 * Zeroes the flags in the .mpu_diag record.
 */
void     tiku_mpu_arch_clear_violation_flags(void);

/**
 * @brief Enable MemManage and set SEGIE in the MPUCTL0 copy.
 *
 * Sets SHCSR.MEMFAULTENA, which tiku_mpu_arch_init_segments() already sets, so
 * an MPU fault enters the MemManage handler; that handler records and resets.
 */
void     tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_AMBIQ_MPU_ARCH_H_ */
