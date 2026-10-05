/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - RA8P1 memory-protection contract.
 *
 * The Cortex-M85's PMSAv8 MPU carries W^X over the image, a stack guard, and
 * an NVM region that is read-only outside a bracketed durable write.  The
 * segment-mask calls keep the kernel's MSP430-form API and only record values.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_MPU_ARCH_H_
#define TIKU_RA8P1_MPU_ARCH_H_

#include <stdint.h>

/** @brief Initial software segment access mask, in MSP430 MPUSAM form. */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

/**
 * @brief Read the software segment access mask (MSP430 MPUSAM form).
 *
 * @return The mask; no hardware reads it
 */
uint16_t tiku_mpu_arch_get_sam(void);

/**
 * @brief Record a segment access mask; the MPU regions do not change.
 *
 * @param sam  New mask
 */
void     tiku_mpu_arch_set_sam(uint16_t sam);

/**
 * @brief MPU control word, in MSP430 MPUCTL0 form.
 *
 * @return 0xA501 once the MPU is on, else 0, with bit 0x0010 set after
 *         tiku_mpu_arch_enable_violation_nmi()
 */
uint16_t tiku_mpu_arch_get_ctl(void);

/** @brief Does nothing: a violation is a MemManage fault, which resets. */
void     tiku_mpu_arch_disable_irq(void);

/** @brief Does nothing: a violation is a MemManage fault, which resets. */
void     tiku_mpu_arch_enable_irq(void);

/**
 * @brief Program the MPU regions from the linker symbols, then enable the
 *        MPU and the caches; also enables the configurable fault handlers.
 */
void     tiku_mpu_arch_init_segments(void);

/** @brief Reset the software mask and make the NVM region read-only. */
void     tiku_mpu_arch_set_default_protection(void);

/**
 * @brief Set one segment's permission bits in the software mask; the MPU
 *        regions do not change.
 *
 * @param seg   Segment index
 * @param perm  Permission bits, 3 per segment
 */
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Open the NVM write window: the MPU's NVM region becomes writable
 *        and MRAM programming is enabled (MRCPC1.MRCPSEN).
 *
 * @note Every unlock needs the matching tiku_mpu_arch_lock_nvm(), which
 *       flushes the MRAM write buffer.  Outside a window the region is
 *       read-only, and a store to it faults.
 * @return State to hand back to tiku_mpu_arch_lock_nvm()
 */
uint16_t tiku_mpu_arch_unlock_nvm(void);

/**
 * @brief Flush the MRAM write buffer and restore the state an unlock saved;
 *        a nested lock leaves the outer window open.
 *
 * @param saved_state  Value from tiku_mpu_arch_unlock_nvm()
 */
void     tiku_mpu_arch_lock_nvm(uint16_t saved_state);

/**
 * @brief Violation flags, from the fault record.
 *
 * @return 0x0002 (SEG1) when the last fault record is a MemManage access
 *         violation, else 0
 */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/** @brief Clear the violation flags and the fault record they come from. */
void     tiku_mpu_arch_clear_violation_flags(void);

/**
 * @brief Set the MPUSEGIE bit in the reported control word; a violation
 *        still resets through the MemManage handler.
 */
void     tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_RA8P1_MPU_ARCH_H_ */
