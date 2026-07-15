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

/*---------------------------------------------------------------------------*/
/* ARMv8-M MPU regions: SRAM W^X + stack guard (hardening, 2026-07 D.1)       */
/*---------------------------------------------------------------------------*/
/*
 * What the MPU adds HERE (and deliberately does not):
 *
 *   - RRAM is NOT re-gated by the MPU.  The RRAMC WEN gate is the durable
 *     write protection (above), and layering an MPU-RO window on top would
 *     re-couple the MPU to every NVM write path for no gain.  RRAM rides
 *     the PRIVDEFENA background map (Normal, RX).
 *   - SRAM becomes eXecute-Never (W^X): three non-overlapping regions --
 *     PMSAv8 overlap is UNPREDICTABLE, so the span is split around the
 *     guard exactly like the rp2350 port.
 *   - A 4 KB read-only stack guard sits STACK_RESERVED below the stack
 *     top.  Sizing copies rp2350's post-BASIC lesson (memory: a 32-byte
 *     guard is LEAPT by KB-sized locals): 32 KB reserve covers BASIC's
 *     10-24 KB frames with margin, and a 4 KB guard cannot be jumped by
 *     any frame in the tree.
 *   - LM20's RAM2 bank (the tier arena) gets RW+XN too.
 *
 * Fault policy: MEMFAULTENA is deliberately NOT set -- a guard hit or an
 * SRAM-execute escalates to HardFault, the SAME observable as a store
 * through the closed WEN gate.  One loud failure mode per port.
 */

#define NRF_SCS_MPU_TYPE   (*(volatile uint32_t *)0xE000ED90UL)
#define NRF_SCS_MPU_CTRL   (*(volatile uint32_t *)0xE000ED94UL)
#define NRF_SCS_MPU_RNR    (*(volatile uint32_t *)0xE000ED98UL)
#define NRF_SCS_MPU_RBAR   (*(volatile uint32_t *)0xE000ED9CUL)
#define NRF_SCS_MPU_RLAR   (*(volatile uint32_t *)0xE000EDA0UL)
#define NRF_SCS_MPU_MAIR0  (*(volatile uint32_t *)0xE000EDC0UL)

#define NRF_MPU_CTRL_ENABLE      (1UL << 0)
#define NRF_MPU_CTRL_PRIVDEFENA  (1UL << 2)
#define NRF_MPU_RBAR_XN          (1UL << 0)
#define NRF_MPU_RBAR_AP_RW_ANY   (1UL << 1)   /* AP[2:1]=01: RW, any priv  */
#define NRF_MPU_RBAR_AP_RO_ANY   (3UL << 1)   /* AP[2:1]=11: RO, any priv  */
#define NRF_MPU_RLAR_EN          (1UL << 0)

/* Stack budget + guard (rp2350-proven values; keep in lockstep with the
 * TikuBench MPU test constants there). */
#define NRF_MPU_STACK_RESERVED_BYTES  32768U
#define NRF_MPU_STACK_GUARD_BYTES     4096U

extern uint32_t __sram_start;
extern uint32_t __stack;
#if defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
extern uint32_t __ram2_start;
extern uint32_t __ram2_end;
#endif

static inline void nrf_mpu_dsb_isb(void)
{
    __asm__ volatile ("dsb 0xF" ::: "memory");
    __asm__ volatile ("isb 0xF" ::: "memory");
}

/** @brief Program one PMSAv8 region (AttrIndx 0, SH=Non-shareable). */
static void nrf_mpu_program_region(uint32_t region, uint32_t base,
                                   uint32_t end_inclusive,
                                   uint32_t ap_bits, uint32_t xn_bit)
{
    NRF_SCS_MPU_RNR  = region;
    NRF_SCS_MPU_RBAR = (base & ~0x1FUL) | (0UL << 3) | ap_bits | xn_bit;
    /* RLAR LIMIT = high bits of the inclusive limit; AttrIndx=0; EN.
     * (Clear the low 5 bits -- setting them would select AttrIndx 15,
     * the rp2350 port's hard-won MAIR footgun.) */
    NRF_SCS_MPU_RLAR = (end_inclusive & ~0x1FUL) | (0UL << 1)
                     | NRF_MPU_RLAR_EN;
}

/** @brief Program the W^X + stack-guard region set and enable the MPU. */
static void nrf_mpu_program_regions(void)
{
    uint32_t sram_base  = (uint32_t)(uintptr_t)&__sram_start;
    uint32_t stack_top  = (uint32_t)(uintptr_t)&__stack;
    uint32_t guard_end  = stack_top - NRF_MPU_STACK_RESERVED_BYTES;
    uint32_t guard_base = guard_end - NRF_MPU_STACK_GUARD_BYTES;

    /* MAIR0 attr 0 = Normal memory, non-cacheable (0x44). */
    NRF_SCS_MPU_MAIR0 = 0x44UL;

    /* R0: SRAM low span (statics + free space), RW + XN. */
    nrf_mpu_program_region(0U, sram_base, guard_base - 1U,
                           NRF_MPU_RBAR_AP_RW_ANY, NRF_MPU_RBAR_XN);
    /* R1: the stack guard, RO + XN -- a descending stack that leaves its
     * 32 KB reserve faults here instead of silently eating statics. */
    nrf_mpu_program_region(1U, guard_base, guard_end - 1U,
                           NRF_MPU_RBAR_AP_RO_ANY, NRF_MPU_RBAR_XN);
    /* R2: the live stack reserve, RW + XN. */
    nrf_mpu_program_region(2U, guard_end, stack_top - 1U,
                           NRF_MPU_RBAR_AP_RW_ANY, NRF_MPU_RBAR_XN);
#if defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
    /* R3: RAM2 upper bank (tier arena), RW + XN.  The M33 only ever
     * reads/writes it; FLPR and EasyDMA are bus masters the MPU does not
     * govern, so the coprocessor paths are unaffected. */
    nrf_mpu_program_region(3U, (uint32_t)(uintptr_t)&__ram2_start,
                           (uint32_t)(uintptr_t)&__ram2_end - 1U,
                           NRF_MPU_RBAR_AP_RW_ANY, NRF_MPU_RBAR_XN);
#endif

    /* Enable with the privileged background map (RRAM RX, peripherals,
     * and any SRAM outside the regions keep default-map access). */
    NRF_SCS_MPU_CTRL = NRF_MPU_CTRL_ENABLE | NRF_MPU_CTRL_PRIVDEFENA;
    nrf_mpu_dsb_isb();
}

/**
 * @brief Initialise the MPU: default SAM shadow, WEN closed, and the
 *        ARMv8-M W^X + stack-guard regions (see the block comment above).
 */
void tiku_mpu_arch_init_segments(void)
{
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
    rramc_wen_set(0UL);
    nrf_mpu_program_regions();
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

/* Stack floor = the top of the MPU stack guard.  The SRAM map is now
 * [statics .. __end__][R0 free space][R1 guard 4K][R2 stack reserve 32K]
 * [__stack]: the paintable/measurable stack span is exactly the R2
 * reserve -- painting below guard_end would write into the read-only
 * guard and HardFault at boot.  The 32 KB reserve still covers BASIC's
 * deepest frames (10-24 KB) without saturating the measurement. */
extern uint32_t __sram_end;
uint32_t tiku_stack_arch_bottom(void)
{
    return (uint32_t)(uintptr_t)&__stack - NRF_MPU_STACK_RESERVED_BYTES;
}
