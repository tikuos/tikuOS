/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - Apollo 510 (Cortex-M55) MPU driver — real ARMv8-M W^X
 *
 * Ports the RP2350 driver (arch/arm-rp2350/tiku_mpu_arch.c) to the M55,
 * re-pinned to the Apollo memory map and using the vendored CMSIS MPU helpers
 * (mpu_armv8.h) instead of hand-rolled RBAR/RLAR. The kernel MPU layer keeps
 * its MSP430 SAM/segment API as software bookkeeping.
 *
 * NVM region note (Apollo510-specific): on RP2350 the NVM tier pool lives in
 * volatile .bss, so .uninit (persist vars only) can be RO-locked and SEG3's W
 * bit moves real hardware permissions. On Apollo510 the NVM tier pool
 * (tier_nvm_buf — the backing store for per-process NVM memory) shares .uninit
 * (mem port B1), and that pool IS written at runtime. RO-locking .uninit would
 * fault those tier writes, so here .uninit stays RW+XN and the unlock/lock are
 * SAM bookkeeping only — they still drive the mem-port-C MRAM flush through the
 * generic layer. The W^X guarantee that matters (code is RX, every data region
 * is execute-never) is fully enforced.
 *
 * Seven non-overlapping regions (this M55 reports 16, so headroom is ample):
 *   0  NVM   .uninit (DTCM)               RW + XN   (writable: holds NVM tier)
 *   1  TEXT  MRAM __flash_start..end      RX            (code + rodata)
 *   2  SRAM  DTCM start..uninit_start     RW + XN       (.data/.bss/.mpu_diag)
 *   3  SRAM  uninit_end..stack_guard      RW + XN       (free middle)
 *   4  GUARD 32 B below the stack budget  RO + XN       (stack-overflow trip)
 *   5  SRAM  guard+32..__sram_end         RW + XN       (live stack)
 *   6  SSRAM 0x20080000 + 3 MB            RW + XN       (tier buffers, snapshot)
 * MAIR0[0] = Normal Write-Back R/W-allocate so the M55 L1 caches keep working
 * (RP2350 used Non-cacheable — it has no cache). PRIVDEFENA lets peripherals
 * (0x40000000+), the SCS/MPU (0xE0000000+) and the bootrom keep the default
 * privileged policy without burning regions. MemManage is enabled at priority
 * 0; a violation records into the warm-durable .mpu_diag and resets.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "apollo510.h"            /* CMSIS: MPU/SCB/NVIC + mpu_armv8.h */
#include <hal/tiku_cpu.h>         /* tiku_cpu_irq_disable/enable */
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Linker symbols for the protected regions                                  */
/*---------------------------------------------------------------------------*/

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;
extern uint32_t __sram_start;     /* DTCM base   */
extern uint32_t __sram_end;       /* DTCM top (= __stack) */
extern uint32_t __flash_start;    /* MRAM code window base */
extern uint32_t __flash_end;      /* MRAM code window end (below the NVM mirror) */

/* The 3 MB shared SRAM (powered in crt_early): tier buffers + the C snapshot.
 * Covered whole so nothing in it is ever executable. */
#define AMBIQ_SSRAM_BASE   0x20080000UL
#define AMBIQ_SSRAM_SIZE   (3UL * 1024UL * 1024UL)

/*---------------------------------------------------------------------------*/
/* Software-bookkept MSP430-style register file (parity for portable tests)  */
/*---------------------------------------------------------------------------*/

static uint16_t stub_mpuctl0;
static uint16_t stub_mpuctl1;     /* violation flags */
static uint16_t stub_mpusam = TIKU_MPU_DEFAULT_SAM;
static uint16_t stub_mpusegb1;
static uint16_t stub_mpusegb2;

/*---------------------------------------------------------------------------*/
/* Persistent diagnostic state (.mpu_diag NOLOAD, outside the NVM region so   */
/* the fault handler can write it; survives the post-fault reset).            */
/*---------------------------------------------------------------------------*/

#define TIKU_MPU_DIAG_MAGIC  0x4D505550U   /* 'MPUP' */

struct tiku_mpu_diag {
    uint32_t magic;
    uint32_t violation_count;     /* total faults across warm boots */
    uint32_t last_fault_addr;     /* MMFAR snapshot */
    uint32_t last_fault_cfsr;     /* full CFSR */
    uint32_t last_fault_hfsr;     /* HFSR (bit 30 FORCED = escalated) */
    uint32_t last_fault_ipsr;     /* handling exception number */
    uint32_t expect_fault;        /* test scaffold: 1 armed -> 2 observed */
};

__attribute__((section(".mpu_diag")))
static volatile struct tiku_mpu_diag mpu_diag;

/*---------------------------------------------------------------------------*/
/* Region map + hardware helpers                                             */
/*---------------------------------------------------------------------------*/

#define MPU_REGION_NVM         0U
#define MPU_REGION_TEXT        1U
#define MPU_REGION_SRAM_LO     2U
#define MPU_REGION_SRAM_MID    3U
#define MPU_REGION_STACK_GUARD 4U
#define MPU_REGION_SRAM_TOP    5U
#define MPU_REGION_SSRAM       6U

/* Stack budget: the guard sits this far below __sram_end; a descending stack
 * that consumes more than this faults instead of corrupting .bss/.uninit.
 * Generous (DTCM is 512 KB) so a deep BASIC expression can't false-trip it. */
#define MPU_STACK_RESERVED_BYTES   32768U
#define MPU_STACK_GUARD_BYTES      32U

#define TIKU_MMFSR_MMARVALID       (1UL << 7)   /* CFSR/MMFSR: MMFAR valid */

/* AttrIndx 0 = Normal memory, inner+outer Write-Back, non-transient,
 * read+write allocate -> the L1 caches stay enabled for MRAM/SSRAM (TCM
 * bypasses the cache regardless, so it is harmless there). */
#define MPU_ATTR_NORMAL_WBWA \
    ARM_MPU_ATTR(ARM_MPU_ATTR_MEMORY_(1U, 1U, 1U, 1U), \
                 ARM_MPU_ATTR_MEMORY_(1U, 1U, 1U, 1U))

/* ro: 1 = read-only, 0 = read-write.  xn: 1 = execute-never, 0 = executable.
 * NP = 1 (privileged + unprivileged), SH = Non-shareable, AttrIndx 0. */
static void mpu_region(uint32_t rnr, uint32_t base, uint32_t limit_incl,
                       uint32_t ro, uint32_t xn) {
    ARM_MPU_SetRegion(rnr,
        ARM_MPU_RBAR(base, ARM_MPU_SH_NON, ro, 1U, xn),
        ARM_MPU_RLAR(limit_incl, 0U));
    __DSB();
    __ISB();
}

/* Region 0 = .uninit, RW + XN. See the NVM region note in the file header:
 * .uninit holds the writable NVM tier pool, so it is NOT RO-locked. */
static void mpu_set_nvm(void) {
    mpu_region(MPU_REGION_NVM,
               (uint32_t)(uintptr_t)&__uninit_start,
               (uint32_t)(uintptr_t)&__uninit_end - 1U,
               0U /* RW */, 1U /* XN */);
}

static inline uint32_t mpu_stack_guard_base(void) {
    return (uint32_t)(uintptr_t)&__sram_end -
           MPU_STACK_RESERVED_BYTES - MPU_STACK_GUARD_BYTES;
}

/*---------------------------------------------------------------------------*/
/* SAM/CTL bookkeeping + HAL surface                                         */
/*---------------------------------------------------------------------------*/

uint16_t tiku_mpu_arch_get_sam(void) { return stub_mpusam; }

void tiku_mpu_arch_set_sam(uint16_t sam) {
    stub_mpuctl0 = 0xA500U;             /* mirror MSP430 password write */
    stub_mpusam  = sam;
    stub_mpuctl0 = 0xA500U | 0x0001U;   /* password | enable */
    /* Bookkeeping only on Apollo510 — .uninit stays RW+XN (the NVM tier pool
     * shares it), and SEG1/SEG2 hardware is fixed (flash can't take stores,
     * SRAM must stay XN regardless of the SAM bits). */
}

uint16_t tiku_mpu_arch_get_ctl(void) { return stub_mpuctl0; }

void tiku_mpu_arch_disable_irq(void) { tiku_cpu_irq_disable(); }
void tiku_mpu_arch_enable_irq(void)  { tiku_cpu_irq_enable(); }

void tiku_mpu_arch_init_segments(void) {
    /* Cold-boot detect: zero + magic on first power-up; keep counters across
     * a warm (post-fault) reset so the violation survives to be read. */
    if (mpu_diag.magic != TIKU_MPU_DIAG_MAGIC) {
        mpu_diag.magic            = TIKU_MPU_DIAG_MAGIC;
        mpu_diag.violation_count  = 0U;
        mpu_diag.last_fault_addr  = 0U;
        mpu_diag.last_fault_cfsr  = 0U;
        mpu_diag.last_fault_hfsr  = 0U;
        mpu_diag.last_fault_ipsr  = 0U;
        mpu_diag.expect_fault     = 0U;
    }

    /* Mirror the MSP430 SEGB layout so code reading them sees a familiar shape. */
    stub_mpusegb1 = 0x0800U;
    stub_mpusegb2 = 0x0C00U;

    ARM_MPU_Disable();                  /* DSB/ISB inside */
    ARM_MPU_SetMemAttr(0U, MPU_ATTR_NORMAL_WBWA);

    {
        uint32_t guard = mpu_stack_guard_base();

        mpu_set_nvm();                                          /* region 0: RW+XN */
        mpu_region(MPU_REGION_TEXT,
                   (uint32_t)(uintptr_t)&__flash_start,
                   (uint32_t)(uintptr_t)&__flash_end - 1U,
                   1U /* RO */, 0U /* exec OK */);              /* region 1 */
        mpu_region(MPU_REGION_SRAM_LO,
                   (uint32_t)(uintptr_t)&__sram_start,
                   (uint32_t)(uintptr_t)&__uninit_start - 1U,
                   0U /* RW */, 1U /* XN */);                   /* region 2 */
        mpu_region(MPU_REGION_SRAM_MID,
                   (uint32_t)(uintptr_t)&__uninit_end,
                   guard - 1U, 0U, 1U);                         /* region 3 */
        mpu_region(MPU_REGION_STACK_GUARD,
                   guard, guard + MPU_STACK_GUARD_BYTES - 1U,
                   1U /* RO */, 1U /* XN */);                   /* region 4 */
        mpu_region(MPU_REGION_SRAM_TOP,
                   guard + MPU_STACK_GUARD_BYTES,
                   (uint32_t)(uintptr_t)&__sram_end - 1U,
                   0U, 1U);                                     /* region 5 */
        mpu_region(MPU_REGION_SSRAM,
                   AMBIQ_SSRAM_BASE,
                   AMBIQ_SSRAM_BASE + AMBIQ_SSRAM_SIZE - 1U,
                   0U, 1U);                                     /* region 6 */
    }

    /* Enable MPU + PRIVDEFENA. HFNMIENA off by default (a buggy fault handler
     * silently no-ops rather than locking the chip up); opt in for hardened
     * builds. ARM_MPU_Enable ORs in ENABLE + DSB/ISB. */
#ifndef TIKU_MPU_HFNMI_ENFORCE
#define TIKU_MPU_HFNMI_ENFORCE 0
#endif
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk
#if TIKU_MPU_HFNMI_ENFORCE
                   | MPU_CTRL_HFNMIENA_Msk
#endif
    );

    /* MemManage at highest configurable priority + enabled, so MPU faults
     * vector to MemManage instead of escalating to HardFault. */
    NVIC_SetPriority(MemoryManagement_IRQn, 0U);
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
    __DSB();
    __ISB();
}

void tiku_mpu_arch_set_default_protection(void) {
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm) {
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t mask  = (uint16_t)0x07U << shift;
    uint16_t sam   = (uint16_t)((stub_mpusam & ~mask) |
                                (((uint16_t)perm & 0x07U) << shift));
    tiku_mpu_arch_set_sam(sam);
}

uint16_t tiku_mpu_arch_unlock_nvm(void) {
    /* .uninit is already RW (see mpu_set_nvm), so this is bookkeeping: OR in
     * the W bits for SAM parity and hand back the prior word. The matching
     * generic tiku_mpu_lock_nvm() still drives tiku_mem_arch_nvm_flush(), so
     * the MRAM mirror (mem port C) commits exactly as before. */
    uint16_t saved = stub_mpusam;
    stub_mpusam = (uint16_t)(saved | 0x0222U);
    return saved;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved_state) {
    tiku_mpu_arch_set_sam(saved_state);
}

uint16_t tiku_mpu_arch_get_violation_flags(void)   { return stub_mpuctl1; }
void     tiku_mpu_arch_clear_violation_flags(void) { stub_mpuctl1 = 0U; }

void tiku_mpu_arch_enable_violation_nmi(void) {
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
    __DSB();
    __ISB();
    stub_mpuctl0 |= 0x0010U;            /* mirror MPU_SEGIE */
}

/*---------------------------------------------------------------------------*/
/* Diagnostics consumed by the generic layer + a minimal violation-test hook */
/*---------------------------------------------------------------------------*/

uint32_t tiku_mpu_arch_violation_count(void)  { return mpu_diag.violation_count; }
uint32_t tiku_mpu_arch_last_fault_addr(void)  { return mpu_diag.last_fault_addr; }
uint32_t tiku_mpu_arch_last_fault_cfsr(void)  { return mpu_diag.last_fault_cfsr; }
uint32_t tiku_mpu_arch_test_expect_fault(void){ return mpu_diag.expect_fault; }
void     tiku_mpu_arch_test_arm_fault(void)   { mpu_diag.expect_fault = 1U; }

void tiku_mpu_arch_test_clear_violation(void) {
    mpu_diag.violation_count = 0U;
    mpu_diag.last_fault_addr = 0U;
    mpu_diag.expect_fault    = 0U;
}

/*---------------------------------------------------------------------------*/
/* Fault handlers — strong overrides of the weak crt_early aliases.          */
/*                                                                            */
/* MPU-safety audit (re-check on every edit; matters only if HFNMIENA=1):    */
/*   SCB->*           SCS 0xE000E000+  -- PRIVDEFENA RW                       */
/*   mpu_diag.*       .mpu_diag (DTCM) -- Region 2 (SRAM_LO) RW + XN          */
/*   stub_mpuctl1     .bss   (DTCM)    -- Region 2 RW + XN                    */
/*   instruction      .text  (MRAM)    -- Region 1 RX                          */
/*---------------------------------------------------------------------------*/

void tiku_ambiq_mem_fault_handler(void) {
    uint32_t cfsr  = SCB->CFSR;
    uint32_t mmfsr = cfsr & 0xFFU;
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(ipsr));

    if (mmfsr & TIKU_MMFSR_MMARVALID) {
        mpu_diag.last_fault_addr = SCB->MMFAR;
    }
    mpu_diag.last_fault_cfsr = cfsr;
    mpu_diag.last_fault_hfsr = SCB->HFSR;
    mpu_diag.last_fault_ipsr = ipsr;
    mpu_diag.violation_count++;
    stub_mpuctl1 |= (uint16_t)mmfsr;
    if (mpu_diag.expect_fault == 1U) {
        mpu_diag.expect_fault = 2U;     /* observed */
    }

    SCB->CFSR = cfsr;                   /* W1C */

    /* A synchronous access violation can't be stepped over; letting the store
     * proceed defeats the MPU. Reset — the post-reset boot (or J-Link) sees
     * the incremented .mpu_diag counter. */
    __DSB();
    NVIC_SystemReset();
    for (;;) { }
}

void tiku_ambiq_hard_fault_handler(void) {
    uint32_t cfsr = SCB->CFSR;
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(ipsr));

    mpu_diag.last_fault_addr = SCB->MMFAR;
    mpu_diag.last_fault_cfsr = cfsr;
    mpu_diag.last_fault_hfsr = SCB->HFSR;
    mpu_diag.last_fault_ipsr = ipsr;
    mpu_diag.violation_count++;
    if (mpu_diag.expect_fault == 1U) {
        mpu_diag.expect_fault = 3U;     /* HardFault path observed */
    }

    __DSB();
    NVIC_SystemReset();
    for (;;) { }
}
