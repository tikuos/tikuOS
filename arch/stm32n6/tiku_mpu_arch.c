/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - STM32N6 memory protection, not yet programmed.
 *
 * The whole image lives in one SRAM window that the boot ROM already marked
 * read-write-execute, so there is nothing this layer can usefully divide yet.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"

/* Reported mask. Kept in a variable so set/get agree, which is what callers
 * check; it does not reach the PMSAv8 registers. */
static uint16_t mpu_sam = TIKU_MPU_DEFAULT_SAM;

uint16_t tiku_mpu_arch_get_sam(void) {
    return mpu_sam;
}

void tiku_mpu_arch_set_sam(uint16_t sam) {
    mpu_sam = sam;
}

uint16_t tiku_mpu_arch_get_ctl(void) {
    /* Zero reads as "MPU disabled", which is the truth on this port. */
    return 0U;
}

void tiku_mpu_arch_disable_irq(void) {
}

void tiku_mpu_arch_enable_irq(void) {
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
    /* Durable data is ordinary SRAM here, so it is already writable. */
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
