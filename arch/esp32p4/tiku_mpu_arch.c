/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - P4 memory protection: the portable state machine.
 *
 * The segment access mask is a software shadow of the MSP430 MPU and
 * enforces nothing.  The firmware profile mirrors MPUCTL0's enable bit; the
 * kernel-test profile reports a zero control word, no protection hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_mpu_hal.h>
#include <hal/tiku_cpu.h>

/** @brief Default segment access mask: read and execute, no write. */
#define TIKU_MPU_DEFAULT_SAM 0x0555u
/** @brief The WRITE bits of the three SAM segment fields. */
#define SAM_WRITE_BITS 0x0222u

static uint16_t mpu_sam = TIKU_MPU_DEFAULT_SAM;
/** @brief MPUCTL0 shadow: the password and enable bit once a mask is set. */
static uint16_t mpu_ctl;

uint16_t tiku_mpu_arch_get_sam(void) { return mpu_sam; }
void tiku_mpu_arch_set_sam(uint16_t sam)
{
    mpu_sam = sam;
#if defined(TIKU_P4_FIRMWARE)
    mpu_ctl = 0xA500u | 0x0001u;
#endif
}
uint16_t tiku_mpu_arch_get_ctl(void) { return mpu_ctl; }
void tiku_mpu_arch_disable_irq(void) { tiku_cpu_irq_disable(); }
void tiku_mpu_arch_enable_irq(void) { tiku_cpu_irq_enable(); }
void tiku_mpu_arch_init_segments(void) { tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM); }
void tiku_mpu_arch_set_default_protection(void)
{
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}
void tiku_mpu_arch_module_window_exec(int enable) { (void)enable; }
void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)
{
    uint16_t shift = (uint16_t)(seg * 4u), mask = (uint16_t)(0x07u << shift);
    tiku_mpu_arch_set_sam((uint16_t)((mpu_sam & ~mask) |
                                     (((uint16_t)perm & 0x07u) << shift)));
}
uint16_t tiku_mpu_arch_unlock_nvm(void)
{
    uint16_t saved = mpu_sam;
    tiku_mpu_arch_set_sam((uint16_t)(saved | SAM_WRITE_BITS));
    return saved;
}
void tiku_mpu_arch_lock_nvm(uint16_t saved_state) { tiku_mpu_arch_set_sam(saved_state); }
uint16_t tiku_mpu_arch_get_violation_flags(void) { return 0; }
void tiku_mpu_arch_clear_violation_flags(void) { }
void tiku_mpu_arch_enable_violation_nmi(void) { }
