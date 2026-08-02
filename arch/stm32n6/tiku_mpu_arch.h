/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - STM32N6 memory-protection contract.
 *
 * The Cortex-M55 carries a PMSAv8 MPU, but this port does not program regions
 * yet, so the calls report an unprotected map rather than a fabricated one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_MPU_ARCH_H_
#define TIKU_STM32N6_MPU_ARCH_H_

#include <stdint.h>

/** @brief Segment access mask reported when no regions are programmed. */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

/** @brief Segment access mask. @return The current mask */
uint16_t tiku_mpu_arch_get_sam(void);

/** @brief Set the segment access mask. @param sam  New mask */
void     tiku_mpu_arch_set_sam(uint16_t sam);

/** @brief MPU control word. @return Control register image */
uint16_t tiku_mpu_arch_get_ctl(void);

/** @brief Mask MPU violation interrupts. */
void     tiku_mpu_arch_disable_irq(void);

/** @brief Unmask MPU violation interrupts. */
void     tiku_mpu_arch_enable_irq(void);

/** @brief Build the segment layout from the linker symbols. */
void     tiku_mpu_arch_init_segments(void);

/** @brief Apply the default protection policy. */
void     tiku_mpu_arch_set_default_protection(void);

/**
 * @brief Set permissions on one segment.
 *
 * @param seg   Segment index
 * @param perm  Permission bits
 */
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Make the durable region writable.
 *
 * @return State to hand back to tiku_mpu_arch_lock_nvm()
 * @note Durable data is SRAM here, so nothing is gated.
 */
uint16_t tiku_mpu_arch_unlock_nvm(void);

/** @brief Restore protection. @param saved_state  Value from unlock */
void     tiku_mpu_arch_lock_nvm(uint16_t saved_state);

/** @brief Violation flags. @return Latched violation bits */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/** @brief Clear latched violation flags. */
void     tiku_mpu_arch_clear_violation_flags(void);

/** @brief Route violations to the NMI handler. */
void     tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_STM32N6_MPU_ARCH_H_ */
