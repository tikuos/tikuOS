/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - ESP32-C61 memory-protection contract.
 *
 * The segment access mask is a software shadow of the MSP430 MPU, which the
 * portable tests check; a locked PMP entry guards the first 4 KB (NULL).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_MPU_ARCH_H_
#define TIKU_ESP32C61_MPU_ARCH_H_

#include <stdint.h>

/** @brief Default segment access mask: read and execute, no write. */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

/** @brief Segment access mask. @return The current mask */
uint16_t tiku_mpu_arch_get_sam(void);

/**
 * @brief Set the segment access mask; the mask enforces nothing.
 *
 * Leaves the MPUCTL0 shadow at password | enable (0xA501), which the
 * portable MPU tests read back.
 *
 * @param sam  New mask
 */
void     tiku_mpu_arch_set_sam(uint16_t sam);

/** @brief The MPUCTL0 shadow. @return Password and enable bits */
uint16_t tiku_mpu_arch_get_ctl(void);

/** @brief Mask interrupts; this port raises no MPU violation interrupt. */
void     tiku_mpu_arch_disable_irq(void);

/** @brief Unmask interrupts; this port raises no MPU violation interrupt. */
void     tiku_mpu_arch_enable_irq(void);

/**
 * @brief Reset the mask to its default and lock the PMP NULL guard.
 *
 * Any access below 4 KB then faults.  The lock holds until reset, so a
 * second call changes only the mask.
 */
void     tiku_mpu_arch_init_segments(void);

/** @brief Reset the mask to TIKU_MPU_DEFAULT_SAM: read and execute, no write. */
void     tiku_mpu_arch_set_default_protection(void);

/**
 * @brief Set permissions on one segment of the mask.
 *
 * @param seg   Segment index 0-2; its field is bits [4*seg+2 : 4*seg]
 * @param perm  TIKU_MPU_READ/WRITE/EXEC flags; higher bits are ignored
 */
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Set the write bit of every segment in the mask.
 *
 * @return The prior mask, for tiku_mpu_arch_lock_nvm(); windows nest
 * @note Durable data is SRAM here, so nothing is gated.
 */
uint16_t tiku_mpu_arch_unlock_nvm(void);

/**
 * @brief Restore the mask.
 *
 * @param saved_state  The value tiku_mpu_arch_unlock_nvm() returned
 */
void     tiku_mpu_arch_lock_nvm(uint16_t saved_state);

/** @brief Violation flags; none latch on this port. @return 0 */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/** @brief Does nothing: no violation flags latch on this port. */
void     tiku_mpu_arch_clear_violation_flags(void);

/** @brief Does nothing: this port has no violation NMI. */
void     tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_ESP32C61_MPU_ARCH_H_ */
