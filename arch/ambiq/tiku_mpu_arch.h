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
 * At this milestone the implementation is a pass-through shim: the
 * unlock/lock-NVM handshake succeeds so persistent writes flow, but no
 * regions are programmed yet. The full W^X driver (ported from RP2350,
 * re-pinned to the Apollo memory map) lands later.
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
