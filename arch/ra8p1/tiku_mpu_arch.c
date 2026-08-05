/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - RA8P1 memory protection, not yet programmed.
 *
 * The whole image lives in one SRAM window that the boot ROM already marked
 * read-write-execute, so there is nothing this layer can usefully divide yet.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"

/* Reported mask.  Kept in a variable so set/get agree, which is what callers
 * check; it does not reach the PMSAv8 registers.
 *
 * The unlock/lock pair being no-ops is CORRECT here rather than lazy: durable
 * data on this port is ordinary SRAM, so it is already writable, and an
 * unbracketed write cannot be silently dropped the way it would be on MSP430.
 * When R6 moves the durable region into MRAM these stop being no-ops, and the
 * kernel's existing discipline of bracketing every durable write is what makes
 * that change safe rather than a sweep through every call site. */
static uint16_t mpu_sam = TIKU_MPU_DEFAULT_SAM;

uint16_t tiku_mpu_arch_get_sam(void) {
    return mpu_sam;
}

void tiku_mpu_arch_set_sam(uint16_t sam) {
    mpu_sam = sam;
}

uint16_t tiku_mpu_arch_get_ctl(void) {
    /*
     * Zero: "MPU disabled", and that is the truth rather than a gap in the
     * bookkeeping.  The other ports mirror MSP430's MPUCTL0 password|enable
     * here BECAUSE they also program real protection -- rp2350 and Ambiq both
     * write MPU_CTRL.  This port programs no PMSAv8 regions, so reporting
     * enabled would claim a protection it does not have, and the two mpu
     * assertions that check for it SHOULD fail until W^X is real here.
     */
    return 0U;
}

void tiku_mpu_arch_disable_irq(void) {
}

void tiku_mpu_arch_enable_irq(void) {
}

void tiku_mpu_arch_init_segments(void) {
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

void tiku_mpu_arch_set_default_protection(void) {
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm) {
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t mask  = (uint16_t)0x07U << shift;

    tiku_mpu_arch_set_sam((uint16_t)((mpu_sam & ~mask) |
                                     (((uint16_t)perm & 0x07U) << shift)));
}

uint16_t tiku_mpu_arch_unlock_nvm(void) {
    /* Bookkeeping, and it is not decorative: the SAM is the kernel's PORTABLE
     * permission model, and callers read it back to check that a durable
     * write is bracketed.  The hardware side is a no-op here because durable
     * data is ordinary SRAM until R6 -- but leaving the model unchanged made
     * unlock indistinguishable from lock to anyone who asked. */
    uint16_t saved = mpu_sam;

    mpu_sam = (uint16_t)(saved | 0x0222U);   /* write bit into every segment */
    return saved;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved_state) {
    tiku_mpu_arch_set_sam(saved_state);
}

uint16_t tiku_mpu_arch_get_violation_flags(void) {
    return 0U;
}

void tiku_mpu_arch_clear_violation_flags(void) {
}

void tiku_mpu_arch_enable_violation_nmi(void) {
}
