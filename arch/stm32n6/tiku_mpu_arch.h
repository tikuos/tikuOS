/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - STM32N6 memory-protection stub.
 *
 * This port programs no region of the Cortex-M55's PMSAv8 MPU: the calls
 * report a disabled MPU and keep the segment access mask in a variable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_MPU_ARCH_H_
#define TIKU_STM32N6_MPU_ARCH_H_

#include <stdint.h>

/** @brief Segment access mask tiku_mpu_arch_get_sam() returns until set. */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

/** @brief Stored segment access mask. @return The mask last set */
uint16_t tiku_mpu_arch_get_sam(void);

/** @brief Store the segment access mask; no MPU register changes. */
void     tiku_mpu_arch_set_sam(uint16_t sam);

/** @brief MPU control word. @return 0: the MPU is disabled */
uint16_t tiku_mpu_arch_get_ctl(void);

/** @brief Does nothing: interrupts are not masked. */
void     tiku_mpu_arch_disable_irq(void);

/** @brief Does nothing. */
void     tiku_mpu_arch_enable_irq(void);

/** @brief Does nothing: no MPU region is programmed. */
void     tiku_mpu_arch_init_segments(void);

/** @brief Does nothing; the stored mask keeps its value. */
void     tiku_mpu_arch_set_default_protection(void);

/** @brief Ignores @p seg and @p perm; the stored mask keeps its value. */
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Make the durable region writable.
 *
 * Durable data is SRAM here and always writable, so nothing changes.
 *
 * @return The stored segment access mask, for tiku_mpu_arch_lock_nvm()
 */
uint16_t tiku_mpu_arch_unlock_nvm(void);

/** @brief Ignores @p saved_state; nothing changes. */
void     tiku_mpu_arch_lock_nvm(uint16_t saved_state);

/** @brief Violation flags. @return 0: no violation is ever latched */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/** @brief Does nothing: no violation is latched. */
void     tiku_mpu_arch_clear_violation_flags(void);

/** @brief Does nothing. */
void     tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_STM32N6_MPU_ARCH_H_ */
