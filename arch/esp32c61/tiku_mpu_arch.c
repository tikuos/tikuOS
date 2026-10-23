/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - ESP32-C61 memory protection: the portable state machine.
 *
 * The segment access mask is a software shadow of the MSP430 MPU and
 * enforces nothing.  A locked PMP entry faults any access below 4 KB (NULL),
 * and PMA entry 12 holds the module window to W^X.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_mpu_hal.h>
#include "tiku_mpu_arch.h"
#include <hal/tiku_cpu.h>
#include "tiku_esp32c61_regs.h"
#include "tiku_psram_arch.h"

/* Order instruction fetches after the PMA change; a host model defines
 * its own. */
#ifndef TIKU_ESP32C61_FENCE_I
#define TIKU_ESP32C61_FENCE_I() __asm__ volatile ("fence.i" ::: "memory")
#endif

/** @brief The WRITE bits of the three SAM segment fields. */
#define TIKU_MPU_SAM_WRITE_BITS  0x0222U

/** @brief Software segment-access-mask shadow (MSP430 SAM model). */
static uint16_t mpu_sam = TIKU_MPU_DEFAULT_SAM;

/** @brief Software MPUCTL0 shadow (password | enable mirror). */
static uint16_t mpu_ctl;

uint16_t tiku_mpu_arch_get_sam(void) {
    return mpu_sam;
}

void tiku_mpu_arch_set_sam(uint16_t sam) {
    mpu_ctl = 0xA500U;                  /* mirror MSP430 password write */
    mpu_sam = sam;
    mpu_ctl = 0xA500U | 0x0001U;        /* password | enable            */
}

uint16_t tiku_mpu_arch_get_ctl(void) {
    return mpu_ctl;
}

void tiku_mpu_arch_disable_irq(void) {
    tiku_cpu_irq_disable();
}

void tiku_mpu_arch_enable_irq(void) {
    tiku_cpu_irq_enable();
}

void tiku_mpu_arch_init_segments(void) {
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
    /* Nothing lives below 4 KB and the bus answers loads there with zeros.
     * PMP entry 0, NAPOT over 4 KB and locked with no permissions, makes any
     * access there fault; the lock holds until reset. */
    ESP32C61_CSR_WRITE(ESP32C61_CSR_PMPADDR0, (0x1000UL / 8UL) - 1UL);
    ESP32C61_CSR_WRITE(ESP32C61_CSR_PMPCFG0,
                       (ESP32C61_CSR_READ(ESP32C61_CSR_PMPCFG0) & ~0xFFUL) |
                       ESP32C61_PMP_LOCK_NAPOT);
}

void tiku_mpu_arch_set_default_protection(void) {
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm) {
    uint16_t shift = (uint16_t)(seg * 4U);
    uint16_t mask  = (uint16_t)(0x07U << shift);

    tiku_mpu_arch_set_sam((uint16_t)((mpu_sam & ~mask) |
                                     (((uint16_t)perm & 0x07U) << shift)));
}

uint16_t tiku_mpu_arch_unlock_nvm(void) {
    uint16_t saved = mpu_sam;

    tiku_mpu_arch_set_sam((uint16_t)(saved | TIKU_MPU_SAM_WRITE_BITS));
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

void tiku_mpu_arch_module_window_exec(int enable) {
    /* PMA entry 12, NAPOT over the module window, is read/execute when
     * enable is non-zero and read/write otherwise.  It takes priority over
     * the PSRAM's read/write entry 13, so the window is never writable and
     * executable at once. */
    ESP32C61_CSR_WRITE(ESP32C61_CSR_PMAADDR12,
                       (TIKU_ESP32C61_MODULE_WINDOW |
                        (TIKU_ESP32C61_MODULE_WINDOW_BYTES / 2UL - 1UL)) >> 2);
    ESP32C61_CSR_WRITE(ESP32C61_CSR_PMACFG12, enable ? ESP32C61_PMA_NAPOT_RX
                                                     : ESP32C61_PMA_NAPOT_RW);
    TIKU_ESP32C61_FENCE_I();
}
