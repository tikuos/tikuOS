/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - ESP32-C61 memory protection: the portable state machine.
 *
 * The MSP430-style segment mask is a software shadow, so the portable MPU
 * tests run one state machine.  PMP binds machine mode only through a locked
 * entry, which cannot reopen for a durable write, so it enforces one rule
 * that never needs to: a NULL guard over the first 4 KB.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_mpu_hal.h>
#include "tiku_mpu_arch.h"
#include "tiku_esp32c61_regs.h"

/** @brief WRITE bits across the three SAM segment fields (MSP430 model,
 *         the 0x0222 the other ports' shadows use). */
#define TIKU_MPU_SAM_WRITE_BITS  0x0222U

/** @brief Software segment-access-mask shadow (MSP430 SAM model). */
static uint16_t mpu_sam = TIKU_MPU_DEFAULT_SAM;

/** @brief Software MPUCTL0 shadow (password | enable mirror). */
static uint16_t mpu_ctl;

uint16_t tiku_mpu_arch_get_sam(void) {
    return mpu_sam;
}

/**
 * @brief Update the software SAM, mirroring the MSP430 MPUCTL0 sequence.
 *
 * Bookkeeping only; the password-write pattern is kept for test parity.
 */
void tiku_mpu_arch_set_sam(uint16_t sam) {
    mpu_ctl = 0xA500U;                  /* mirror MSP430 password write */
    mpu_sam = sam;
    mpu_ctl = 0xA500U | 0x0001U;        /* password | enable            */
}

uint16_t tiku_mpu_arch_get_ctl(void) {
    return mpu_ctl;
}

void tiku_mpu_arch_disable_irq(void) {
}

void tiku_mpu_arch_enable_irq(void) {
}

/**
 * @brief The shadow's default, and the NULL guard.
 *
 * Nothing lives below 4 KB, yet the bus answers loads there with silence, so
 * a NULL dereference would read zeros on.  Locked: a second call is a no-op.
 */
void tiku_mpu_arch_init_segments(void) {
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
    ESP32C61_CSR_WRITE(ESP32C61_CSR_PMPADDR0, (0x1000UL / 8UL) - 1UL);
    ESP32C61_CSR_WRITE(ESP32C61_CSR_PMPCFG0,
                       (ESP32C61_CSR_READ(ESP32C61_CSR_PMPCFG0) & ~0xFFUL) |
                       ESP32C61_PMP_LOCK_NAPOT);
}

/** @brief Restore the default (read+exec, no write) SAM policy. */
void tiku_mpu_arch_set_default_protection(void) {
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

/**
 * @brief Set the 3-bit permission field for one software SAM segment.
 *
 * Each segment occupies 4 bits of the SAM word, the TIKU_MPU_READ/WRITE/EXEC
 * flags in bits [2:0] of it -- the same math as the other ports.
 */
void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm) {
    uint16_t shift = (uint16_t)(seg * 4U);
    uint16_t mask  = (uint16_t)(0x07U << shift);

    tiku_mpu_arch_set_sam((uint16_t)((mpu_sam & ~mask) |
                                     (((uint16_t)perm & 0x07U) << shift)));
}

/**
 * @brief Open an NVM write window in the shadow.
 *
 * @return The prior SAM word; lock_nvm() restores it, so windows nest
 */
uint16_t tiku_mpu_arch_unlock_nvm(void) {
    uint16_t saved = mpu_sam;

    tiku_mpu_arch_set_sam((uint16_t)(saved | TIKU_MPU_SAM_WRITE_BITS));
    return saved;
}

/** @brief Close an NVM write window. @param saved_state  unlock_nvm()'s value */
void tiku_mpu_arch_lock_nvm(uint16_t saved_state) {
    tiku_mpu_arch_set_sam(saved_state);
}

/** @brief Nothing enforces, so nothing is ever flagged. @return 0 */
uint16_t tiku_mpu_arch_get_violation_flags(void) {
    return 0U;
}

void tiku_mpu_arch_clear_violation_flags(void) {
}

void tiku_mpu_arch_enable_violation_nmi(void) {
}

/** @brief No RAM execution window to open: modules are not loaded here yet. */
void tiku_mpu_arch_module_window_exec(int enable) {
    (void)enable;
}
