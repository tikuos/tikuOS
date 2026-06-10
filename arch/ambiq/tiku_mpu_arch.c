/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - Apollo 510 MPU driver (pass-through shim)
 *
 * The Cortex-M55 ARMv8-M MPU is not programmed yet. unlock/lock-NVM
 * succeed (returning a neutral saved-state) so the kernel's persistent
 * writes flow; SAM bookkeeping is tracked in software for parity with
 * the other ports. The full W^X driver (ported from RP2350) lands later.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"

static uint16_t s_sam = TIKU_MPU_DEFAULT_SAM;

uint16_t tiku_mpu_arch_get_sam(void)            { return s_sam; }
void     tiku_mpu_arch_set_sam(uint16_t sam)    { s_sam = sam; }
uint16_t tiku_mpu_arch_get_ctl(void)            { return 0; }
void     tiku_mpu_arch_disable_irq(void)        { }
void     tiku_mpu_arch_enable_irq(void)         { }
void     tiku_mpu_arch_init_segments(void)      { }
void     tiku_mpu_arch_set_default_protection(void) { }

void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm) {
    (void)seg; (void)perm;
}

/* No hardware protection yet, so unlock/lock are a no-op handshake. The
 * non-zero token mirrors "something was unlocked" without meaning. */
uint16_t tiku_mpu_arch_unlock_nvm(void)         { return s_sam; }
void     tiku_mpu_arch_lock_nvm(uint16_t saved) { (void)saved; }

uint16_t tiku_mpu_arch_get_violation_flags(void)   { return 0; }
void     tiku_mpu_arch_clear_violation_flags(void) { }
void     tiku_mpu_arch_enable_violation_nmi(void)  { }
