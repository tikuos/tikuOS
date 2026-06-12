/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - Apollo 510 MPU driver interface
 *
 * Cortex-M55 has the ARMv8-M MPU (same RBAR/RLAR/MAIR/CTRL as the M33).
 * tiku_mpu_arch.c programs a real W^X layout (code RX, every data region XN,
 * plus a stack-overflow guard), re-pinned to the Apollo memory map. The NVM
 * region (.uninit) stays RW+XN because the NVM tier pool shares it, so the
 * SEG3 unlock/lock are SAM bookkeeping that still drive the mem-port-C MRAM
 * flush through the generic layer. See the .c file's header for the rationale.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_MPU_ARCH_H_
#define TIKU_AMBIQ_MPU_ARCH_H_

#include <stdint.h>
#include "tiku.h"

/* Mirrors the RP2350 "no HIFRAM" default so portable tests comparing
 * against TIKU_MPU_DEFAULT_SAM behave the same. */
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

#endif /* TIKU_AMBIQ_MPU_ARCH_H_ */
