/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
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

/** @brief Set the segment access mask. @param sam  New mask */
void     tiku_mpu_arch_set_sam(uint16_t sam);

/** @brief The MPUCTL0 shadow. @return Password and enable bits */
uint16_t tiku_mpu_arch_get_ctl(void);

/** @brief Does nothing: this port raises no MPU violation interrupt. */
void     tiku_mpu_arch_disable_irq(void);

/** @brief Does nothing: this port raises no MPU violation interrupt. */
void     tiku_mpu_arch_enable_irq(void);

/** @brief Reset the mask to its default and lock the PMP NULL guard. */
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

/** @brief Violation flags; none latch on this port. @return 0 */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/** @brief Does nothing: no violation flags latch on this port. */
void     tiku_mpu_arch_clear_violation_flags(void);

/** @brief Does nothing: this port has no violation NMI. */
void     tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_ESP32C61_MPU_ARCH_H_ */
