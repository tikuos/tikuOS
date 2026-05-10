/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - RP2350 MPU stubs
 *
 * The kernel MPU layer treats SRAM/flash as fully-accessible on
 * platforms without protection. We honour the "unlocked" semantics
 * by reporting the default SAM on every read and accepting writes
 * silently.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "tiku_cpu_freq_boot_arch.h"
#include <hal/tiku_cpu.h>

uint16_t tiku_mpu_arch_get_sam(void)                            { return TIKU_MPU_DEFAULT_SAM; }
void     tiku_mpu_arch_set_sam(uint16_t sam)                    { (void)sam; }
uint16_t tiku_mpu_arch_get_ctl(void)                            { return 0U; }
void     tiku_mpu_arch_disable_irq(void)                        { tiku_cpu_irq_disable(); }
void     tiku_mpu_arch_enable_irq(void)                         { tiku_cpu_irq_enable(); }
void     tiku_mpu_arch_init_segments(void)                      { /* no MPU programming */ }
void     tiku_mpu_arch_set_default_protection(void)             { /* no MPU programming */ }
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)  { (void)seg; (void)perm; }
uint16_t tiku_mpu_arch_unlock_nvm(void)                         { return 0U; }
void     tiku_mpu_arch_lock_nvm(uint16_t saved_state)           { (void)saved_state; }
uint16_t tiku_mpu_arch_get_violation_flags(void)                { return 0U; }
void     tiku_mpu_arch_clear_violation_flags(void)              { /* nothing */ }
void     tiku_mpu_arch_enable_violation_nmi(void)               { /* nothing */ }
