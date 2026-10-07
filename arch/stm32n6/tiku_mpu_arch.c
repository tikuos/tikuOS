/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - STM32N6 memory-protection stub.
 *
 * No MPU region is programmed.  get_ctl and the violation flags read 0, the
 * segment access mask lives in a variable, and the other calls do nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include <hal/tiku_cpu.h>

/* Segment access mask for get_sam and set_sam; no MPU register holds it. */
static uint16_t mpu_sam = TIKU_MPU_DEFAULT_SAM;

uint16_t tiku_mpu_arch_get_sam(void) {
    return mpu_sam;
}

void tiku_mpu_arch_set_sam(uint16_t sam) {
    mpu_sam = sam;
}

uint16_t tiku_mpu_arch_get_ctl(void) {
    /* 0: the MPU is disabled. */
    return 0U;
}

void tiku_mpu_arch_disable_irq(void) {
    tiku_cpu_irq_disable();
}

void tiku_mpu_arch_enable_irq(void) {
    tiku_cpu_irq_enable();
}

void tiku_mpu_arch_init_segments(void) {
}

void tiku_mpu_arch_set_default_protection(void) {
}

void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm) {
    (void)seg;
    (void)perm;
}

uint16_t tiku_mpu_arch_unlock_nvm(void) {
    /* Durable data is SRAM here and always writable. */
    return mpu_sam;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved_state) {
    (void)saved_state;
}

uint16_t tiku_mpu_arch_get_violation_flags(void) {
    return 0U;
}

void tiku_mpu_arch_clear_violation_flags(void) {
}

void tiku_mpu_arch_enable_violation_nmi(void) {
}
