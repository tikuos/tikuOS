/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - nRF54L NVM write-gate + MPU HAL
 *
 * The nRF54L stores code and persistent data in RRAM, which is byte-writable
 * in place behind the RRAMC controller's CONFIG.WEN write-enable gate -- the
 * exact same "NVM behind a gate" model as MSP430 FRAM.  The kernel's
 * tiku_mpu_arch_unlock_nvm()/lock_nvm() pair therefore maps to WEN=1/restore,
 * and every persistent write (persist cells, boot counter, hang record, the
 * mem NVM writer) brackets itself in that window.
 *
 * The MSP430-modelled segment-access-mask (SAM) entry points are a SOFTWARE
 * SHADOW, the same bookkeeping model the rp2350 and ambiq ports use: the SAM
 * word and MPUCTL0 password-write sequence are tracked so the portable MPU
 * semantics tests exercise one state machine on every port, while the real
 * enforcement on this part is the WEN gate (a store through the closed gate
 * is a precise bus fault -- fault-not-flag, so the violation-flag queries
 * honestly return 0).  unlock/lock are nest-safe the same way as ambiq: the
 * saved SAM's write bits say whether an outer window is still open, and the
 * WEN restore follows them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_mpu_hal.h>
#include <arch/nordic/tiku_mpu_arch.h>
#include <arch/nordic/tiku_nordic_mdk.h>

#define TIKU_RRAMC_WEN   (1UL << 0)   /* RRAMC_CONFIG.WEN: 1 = writes enabled */

/** @brief WRITE bits across the three SAM segment fields (MSP430 model,
 *         same 0x0222 the rp2350/ambiq shadows use). */
#define TIKU_MPU_SAM_WRITE_BITS  0x0222U

/*---------------------------------------------------------------------------*/
/* Software SAM/CTL shadow (portable MPU state machine)                       */
/*---------------------------------------------------------------------------*/

/** @brief Software segment-access-mask shadow (MSP430 SAM model). */
static uint16_t stub_mpusam = TIKU_MPU_DEFAULT_SAM;
/** @brief Software MPUCTL0 shadow (password | enable mirror). */
static uint16_t stub_mpuctl0;

/*---------------------------------------------------------------------------*/
/* NVM write gate (RRAMC CONFIG.WEN) -- the load-bearing part                 */
/*---------------------------------------------------------------------------*/

/** @brief Open/close the RRAMC write gate. */
static void rramc_wen_set(uint32_t open)
{
    uint32_t cfg = NRF_RRAMC_S->CONFIG;

    NRF_RRAMC_S->CONFIG = open ? (cfg | TIKU_RRAMC_WEN)
                               : (cfg & ~TIKU_RRAMC_WEN);
}

/**
 * @brief Open an NVM write window: SAM bookkeeping + the real WEN flip.
 *
 * ORs the write bits into the SAM shadow (MSP430-path parity) and enables
 * RRAM writes.  Returns the prior SAM word; passing it to lock_nvm()
 * restores both the shadow and the gate, nest-safely (an inner lock inside
 * a still-open outer window sees write bits in the saved SAM and keeps the
 * gate open).
 */
uint16_t tiku_mpu_arch_unlock_nvm(void)
{
    uint16_t saved = stub_mpusam;

    stub_mpusam = (uint16_t)(saved | TIKU_MPU_SAM_WRITE_BITS);
    rramc_wen_set(1UL);
    return saved;
}

/**
 * @brief Close an NVM write window: restore the SAM shadow + the gate.
 *
 * @param saved_state  Value returned by the matching unlock_nvm().
 */
void tiku_mpu_arch_lock_nvm(uint16_t saved_state)
{
    tiku_mpu_arch_set_sam(saved_state);
    rramc_wen_set(((saved_state & TIKU_MPU_SAM_WRITE_BITS) == 0u) ? 0UL : 1UL);
}

/*---------------------------------------------------------------------------*/
/* MSP430-modelled MPU entry points (software shadow)                         */
/*---------------------------------------------------------------------------*/

/** @brief Return the current software SAM value. */
uint16_t tiku_mpu_arch_get_sam(void)
{
    return stub_mpusam;
}

/**
 * @brief Update the software SAM, mirroring the MSP430 MPUCTL0 sequence.
 *
 * Bookkeeping only: the RRAM gate is driven by unlock/lock_nvm above; the
 * password-write pattern is preserved for portable-test parity.
 */
void tiku_mpu_arch_set_sam(uint16_t sam)
{
    stub_mpuctl0 = 0xA500U;             /* mirror MSP430 password write */
    stub_mpusam  = sam;
    stub_mpuctl0 = 0xA500U | 0x0001U;   /* password | enable            */
}

/** @brief Return the current software MPUCTL0 value. */
uint16_t tiku_mpu_arch_get_ctl(void)
{
    return stub_mpuctl0;
}

void tiku_mpu_arch_disable_irq(void) { /* no MPU violation IRQ on this port */ }
void tiku_mpu_arch_enable_irq(void)  { /* no MPU violation IRQ on this port */ }

/**
 * @brief Initialise the MPU model: default SAM, enabled CTL shadow.
 *
 * No ARMv8-M MPU regions are programmed yet (a later hardening step); the
 * real store protection is the RRAMC WEN gate, closed by default.
 */
void tiku_mpu_arch_init_segments(void)
{
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
    rramc_wen_set(0UL);
}

/** @brief Restore the default (read+exec, no write) SAM policy. */
void tiku_mpu_arch_set_default_protection(void)
{
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

/**
 * @brief Set the 3-bit permission field for one software SAM segment.
 *
 * Each segment occupies 4 bits of the SAM word (bits [2:0] of the field are
 * the TIKU_MPU_READ/WRITE/EXEC flags) -- identical math to rp2350/ambiq.
 */
void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)
{
    uint16_t shift = (uint16_t)(seg * 4U);
    uint16_t mask  = (uint16_t)(0x07U << shift);
    uint16_t sam   = (uint16_t)((stub_mpusam & ~mask) |
                                (((uint16_t)perm & 0x07U) << shift));

    tiku_mpu_arch_set_sam(sam);
}

/**
 * @brief Violation flags: honestly 0 -- this part is fault-not-flag.
 *
 * A store through the closed WEN gate raises a precise bus fault; there is
 * no latched violation flag to report (the portable violation-detect test
 * skips its trigger phase on fault-not-flag ports).
 */
uint16_t tiku_mpu_arch_get_violation_flags(void)  { return 0u; }
void     tiku_mpu_arch_clear_violation_flags(void) { /* nothing latched */ }
void     tiku_mpu_arch_enable_violation_nmi(void)  { /* no violation NMI  */ }

/* True stack floor = end of statics.  The nordic SRAM map is
 * [statics .. __end__][free stack space][__stack]: no heap and no MPU stack
 * guard sit between __end__ and the descending stack, so the whole span is
 * paintable (the linker ASSERTs >= 8 KB of it stays free).  Returning __end__
 * -- rather than a fixed top-8 KB floor -- lets tiku_stack_free() measure deep
 * stacks (e.g. BASIC's 10-24 KB) instead of saturating to 0 past 8 KB. */
extern uint32_t __sram_end;
extern uint32_t __end__;
uint32_t tiku_stack_arch_bottom(void)
{
    return (uint32_t)(uintptr_t)&__end__;
}
