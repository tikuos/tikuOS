/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - RP2350 MPU driver interface.
 *
 * Programs six non-overlapping ARMv8-M MPU regions: W^X over flash and SRAM,
 * .uninit read-only outside an NVM window, and a stack guard.  Of the
 * MSP430-style segment permissions only SEG3's write bit reaches the MPU.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_MPU_ARCH_H_
#define TIKU_RP2350_MPU_ARCH_H_

#include <stdint.h>
#include "tiku.h"

/**
 * @brief Default SAM: read and execute on SEG1-SEG3, write on none.
 *
 * This port has no HIFRAM tier, so no segment stays writable.  With bit 9
 * (SEG3 write) clear, the .uninit region is read-only.
 */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

/**
 * @brief Read the software segment-access-map (SAM) register.
 *
 * The SAM holds 3-bit R/W/X permissions for SEG1-SEG3 at bits 0, 4 and 8, in
 * the MSP430 layout.  Of these bits only SEG3's write bit reaches the MPU.
 *
 * @return Current SAM value.
 */
uint16_t tiku_mpu_arch_get_sam(void);

/**
 * @brief Write the software SAM register.
 *
 * Also reprograms the .uninit MPU region: read-write when bit 9 (SEG3 write)
 * is set, read-only otherwise.
 *
 * @param sam  New SAM value.
 */
void     tiku_mpu_arch_set_sam(uint16_t sam);

/**
 * @brief Read the software MPUCTL0 mirror.
 *
 * Holds 0xA501 (password and enable) after a SAM write; bit 4 (MPUSEGIE) is
 * set by tiku_mpu_arch_enable_violation_nmi() until the next SAM write.  The
 * hardware MPU_CTRL register is not read.
 *
 * @return MPUCTL0 mirror value.
 */
uint16_t tiku_mpu_arch_get_ctl(void);

/**
 * @brief Disable IRQs (PRIMASK = 1), bracketing an NVM window.
 */
void     tiku_mpu_arch_disable_irq(void);

/**
 * @brief Re-enable IRQs (PRIMASK = 0).
 */
void     tiku_mpu_arch_enable_irq(void);

/**
 * @brief Program the six MPU regions from linker symbols and enable the MPU.
 *
 * Flash is read-execute, SRAM read-write and never executable, .uninit
 * read-only, and a 4 KB read-only guard sits below the top 32 KB of SRAM.
 * Also enables MemManage at priority 0 and, on a cold boot, clears .mpu_diag.
 *
 * @note Called by tiku_mpu_init(), before
 *       tiku_mpu_arch_set_default_protection().
 */
void     tiku_mpu_arch_init_segments(void);

/**
 * @brief Apply TIKU_MPU_DEFAULT_SAM, leaving the .uninit region read-only.
 *
 * @note Called by tiku_mpu_init() after tiku_mpu_arch_init_segments().
 */
void     tiku_mpu_arch_set_default_protection(void);

/**
 * @brief Set the logical permissions of one segment (SEG1/SEG2/SEG3).
 *
 * Updates the segment's bits in the SAM.  Only SEG3's write bit changes the
 * MPU: flash stays read-execute, and SRAM read-write and never executable.
 *
 * @param seg   Segment index (0 = SEG1, 1 = SEG2, 2 = SEG3).
 * @param perm  Permission bitmask (TIKU_MPU_READ / WRITE / EXEC).
 */
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Open an NVM write window.
 *
 * Sets the write bits of all three segments in the SAM and makes the .uninit
 * MPU region read-write.
 *
 * @return The previous SAM, to pass to tiku_mpu_arch_lock_nvm().
 */
uint16_t tiku_mpu_arch_unlock_nvm(void);

/**
 * @brief Close an NVM write window.
 *
 * Restores the SAM saved by tiku_mpu_arch_unlock_nvm(); the .uninit region
 * goes back to read-only unless that SAM had SEG3's write bit set.
 *
 * @note Commits nothing to flash: tiku_mpu_lock_nvm() flushes the .uninit
 *       image with tiku_mem_arch_nvm_flush_status() before calling this.
 * @param saved_state  Value returned by tiku_mpu_arch_unlock_nvm().
 */
void     tiku_mpu_arch_lock_nvm(uint16_t saved_state);

/**
 * @brief Read the MemManage violation flags.
 *
 * The MemManage handler ORs the MMFSR cause bits into these flags before it
 * resets the chip; the flags survive that reset in the .mpu_diag section.
 *
 * @return Violation flags word, 0 when none are recorded.
 */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/**
 * @brief Clear the MemManage violation flags.
 */
void     tiku_mpu_arch_clear_violation_flags(void);

/**
 * @brief Enable the MemManage handler in SCB.SHCSR.
 *
 * Sets MEMFAULTENA, which tiku_mpu_arch_init_segments() also sets, and bit 4
 * (MPUSEGIE) of the MPUCTL0 mirror.  Without MEMFAULTENA an MPU fault goes to
 * HardFault; the MemManage handler records the fault and resets the chip.
 */
void     tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_RP2350_MPU_ARCH_H_ */
