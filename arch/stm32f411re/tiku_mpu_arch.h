/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_mpu_arch.h - STM32F411RE MPU driver interface
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_MPU_ARCH_H_
#define TIKU_STM32F411_MPU_ARCH_H_

#include <stdint.h>

#define TIKU_MPU_DEFAULT_SAM    0x0555U

uint16_t tiku_mpu_arch_get_sam(void);
void     tiku_mpu_arch_set_sam(uint16_t sam);
uint16_t tiku_mpu_arch_get_ctl(void);
void     tiku_mpu_arch_disable_irq(void);
void     tiku_mpu_arch_enable_irq(void);
void     tiku_mpu_arch_init_segments(void);
void     tiku_mpu_arch_set_default_protection(void);
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);
uint16_t tiku_mpu_arch_unlock_nvm(void);
void     tiku_mpu_arch_lock_nvm(uint16_t saved_state);
uint16_t tiku_mpu_arch_get_violation_flags(void);
void     tiku_mpu_arch_clear_violation_flags(void);
void     tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_STM32F411_MPU_ARCH_H_ */
