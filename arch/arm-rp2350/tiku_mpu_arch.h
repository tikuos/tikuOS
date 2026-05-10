/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - RP2350 MPU stub interface
 *
 * The Cortex-M33 has an ARMv8-M MPU but tikuOS does not program it
 * on the first port — kernel MPU calls become no-ops. The header
 * exists for API parity with the MSP430 port; functions return
 * benign defaults.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_MPU_ARCH_H_
#define TIKU_RP2350_MPU_ARCH_H_

#include <stdint.h>
#include "tiku.h"

/* No HIFRAM concept on RP2350; default SAM mirrors the "no HIFRAM"
 * MSP430 part value so any test that compares against
 * TIKU_MPU_DEFAULT_SAM passes on host-style runs. */
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

#endif /* TIKU_RP2350_MPU_ARCH_H_ */
