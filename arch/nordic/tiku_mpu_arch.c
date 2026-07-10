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
 * The remaining MPU HAL entry points model the MSP430 segment-access-mask
 * MPU; on this port they are honest no-ops for now (ARMv8-M MPU region
 * protection of the persistent region is a later hardening step, like the
 * rp2350 port grew into).  unlock/lock return + restore the prior WEN state,
 * so bracketed writes nest correctly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_mpu_hal.h>
#include <arch/nordic/mdk/nrf54l15.h>

#define TIKU_RRAMC_WEN   (1UL << 0)   /* RRAMC_CONFIG.WEN: 1 = writes enabled */

/*---------------------------------------------------------------------------*/
/* NVM write gate (RRAMC CONFIG.WEN) -- the load-bearing part                 */
/*---------------------------------------------------------------------------*/

uint16_t tiku_mpu_arch_unlock_nvm(void)
{
    uint32_t cfg  = NRF_RRAMC_S->CONFIG;
    uint16_t prev = (uint16_t)(cfg & TIKU_RRAMC_WEN);   /* 0 or 1 */

    NRF_RRAMC_S->CONFIG = cfg | TIKU_RRAMC_WEN;          /* enable writes */
    return prev;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved_state)
{
    uint32_t cfg = NRF_RRAMC_S->CONFIG;

    if (saved_state & TIKU_RRAMC_WEN) {
        NRF_RRAMC_S->CONFIG = cfg | TIKU_RRAMC_WEN;      /* keep enabled */
    } else {
        NRF_RRAMC_S->CONFIG = cfg & ~TIKU_RRAMC_WEN;     /* re-disable   */
    }
}

/*---------------------------------------------------------------------------*/
/* MSP430-modelled MPU entry points -- no-op stubs on this port              */
/*---------------------------------------------------------------------------*/

uint16_t tiku_mpu_arch_get_sam(void)              { return 0u; }
void     tiku_mpu_arch_set_sam(uint16_t sam)      { (void)sam; }
uint16_t tiku_mpu_arch_get_ctl(void)              { return 0u; }
void     tiku_mpu_arch_disable_irq(void)          { /* no MPU violation IRQ */ }
void     tiku_mpu_arch_enable_irq(void)           { /* no MPU violation IRQ */ }
void     tiku_mpu_arch_init_segments(void)        { /* no ARMv8-M MPU yet   */ }
void     tiku_mpu_arch_set_default_protection(void) { /* no-op */ }
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)
{
    (void)seg;
    (void)perm;
}
uint16_t tiku_mpu_arch_get_violation_flags(void)  { return 0u; }
void     tiku_mpu_arch_clear_violation_flags(void) { /* no-op */ }
void     tiku_mpu_arch_enable_violation_nmi(void)  { /* no-op */ }
