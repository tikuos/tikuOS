/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - RP2350 MPU stubs (software-emulated MSP430 MPU)
 *
 * RP2350 has no FRAM and no MSP430-style segment-based MPU; the kernel
 * MPU layer was designed against the MSP430 SAM/CTL register model.
 * To keep the API contract honest on this port we emulate the MSP430
 * MPU in software: every register is a static variable, every set/get
 * touches that variable, and the unlock/lock pair returns/restores the
 * SAM word. The .uninit-backed "NVM" region acts as if it were FRAM
 * for the purposes of the persist + LC layers, so callers that follow
 * the unlock-write-lock protocol get coherent semantics.
 *
 * No HW protection is enforced — we just bookkeep what the caller said
 * — but every test_mem_mpu assertion is satisfied (init defaults,
 * unlock returns previous SAM, lock restores it, custom-base unlock
 * works, segment perms set independently, ...).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "tiku_cpu_freq_boot_arch.h"
#include <hal/tiku_cpu.h>

/*---------------------------------------------------------------------------*/
/* Software-emulated MSP430 MPU registers                                    */
/*---------------------------------------------------------------------------*/

/* MPUCTL0: holds the password (high byte) and ENABLE / SEGIE bits.
 * We mimic the MSP430 behaviour where every write to the SAM
 * re-asserts the password and ENABLE bit. */
static uint16_t stub_mpuctl0;
static uint16_t stub_mpuctl1;   /* violation flags */
static uint16_t stub_mpusam = TIKU_MPU_DEFAULT_SAM;
static uint16_t stub_mpusegb1;
static uint16_t stub_mpusegb2;

/*---------------------------------------------------------------------------*/
/* HAL implementation                                                        */
/*---------------------------------------------------------------------------*/

uint16_t tiku_mpu_arch_get_sam(void)
{
    return stub_mpusam;
}

void tiku_mpu_arch_set_sam(uint16_t sam)
{
    stub_mpuctl0 = 0xA500U;             /* password */
    stub_mpusam  = sam;
    stub_mpuctl0 = 0xA500U | 0x0001U;   /* password | enable */
}

uint16_t tiku_mpu_arch_get_ctl(void)
{
    return stub_mpuctl0;
}

void tiku_mpu_arch_disable_irq(void) { tiku_cpu_irq_disable(); }
void tiku_mpu_arch_enable_irq(void)  { tiku_cpu_irq_enable(); }

void tiku_mpu_arch_init_segments(void)
{
    /* Mirror the MSP430 host stub layout: segment boundaries at the
     * upper third / two-thirds of the 64 KB MPU window. The values
     * are >> 4 because MSP430 SEGB registers count in 16-byte units;
     * we keep the same encoding so any code reading them sees the
     * familiar shape. */
    stub_mpusegb1 = 0x0800U;   /* 0x8000 >> 4 */
    stub_mpusegb2 = 0x0C00U;   /* 0xC000 >> 4 */
}

void tiku_mpu_arch_set_default_protection(void)
{
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)
{
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t mask  = (uint16_t)0x07U << shift;
    uint16_t sam   = tiku_mpu_arch_get_sam();

    sam = (uint16_t)((sam & ~mask) |
                     (((uint16_t)perm & 0x07U) << shift));
    tiku_mpu_arch_set_sam(sam);
}

uint16_t tiku_mpu_arch_unlock_nvm(void)
{
    /* Return the saved SAM so the caller can restore it via lock_nvm.
     * Setting the write bit (0x222 = WRITE in every segment slot)
     * mirrors what the MSP430 driver does to grant FRAM writes. */
    uint16_t saved = tiku_mpu_arch_get_sam();
    tiku_mpu_arch_set_sam((uint16_t)(saved | 0x0222U));
    return saved;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved_state)
{
    tiku_mpu_arch_set_sam(saved_state);
}

uint16_t tiku_mpu_arch_get_violation_flags(void)
{
    return stub_mpuctl1;
}

void tiku_mpu_arch_clear_violation_flags(void)
{
    stub_mpuctl1 = 0U;
}

void tiku_mpu_arch_enable_violation_nmi(void)
{
    stub_mpuctl0 |= 0x0010U;   /* MPUSEGIE bit */
}
