/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - Apollo510 (Cortex-M55) MPU driver, ARMv8-M W^X.
 *
 * Code is RX and every data region execute-never, with a guard below the stack.
 * .uninit is read-only outside tiku_mpu_unlock_nvm() windows, and the matching
 * lock drives the MRAM flush.  Region map below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "apollo510.h"            /* CMSIS: MPU/SCB/NVIC + mpu_armv8.h */
#include <hal/tiku_cpu.h>         /* tiku_cpu_irq_disable/enable */
#include <kernel/shell/basic/tiku_basic_module.h>  /* TIKU_MODULE_EXEC_* */
#include <stdint.h>

/*
 * Eight non-overlapping regions (this M55 reports 16):
 *   0  NVM   .uninit (DTCM)               RO + XN   (RW in an unlock window)
 *   1  TEXT  MRAM __flash_start..end      RX            (code + rodata)
 *   2  SRAM  DTCM start..uninit_start     RW + XN       (.data/.bss/.mpu_diag)
 *   3  SRAM  uninit_end..stack_guard      RW + XN       (free middle)
 *   4  GUARD 4 KB below the stack budget  RO + XN       (stack-overflow trip)
 *   5  SRAM  guard+4K..__sram_end         RW + XN       (live stack)
 *   6  SSRAM 0x20080000 + 3 MB            RW + XN       (tier, snapshot)
 *   7  MOD   module ITCM window           RW + XN   (RO + X while it runs)
 * MAIR0[0] = Normal Write-Back R/W-allocate so the M55 L1 caches keep working.
 * PRIVDEFENA lets peripherals (0x40000000+), the SCS/MPU (0xE0000000+) and the
 * bootrom keep the default privileged policy without using regions.  MemManage
 * is enabled at priority 0; a violation records into the warm-durable
 * .mpu_diag and resets.
 */

/*---------------------------------------------------------------------------*/
/* LINKER SYMBOLS                                                            */
/*---------------------------------------------------------------------------*/

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;
extern uint32_t __sram_start;     /* DTCM base   */
extern uint32_t __sram_end;       /* DTCM top (= __stack) */
extern uint32_t __tiku_stack_bottom;
extern uint32_t __tiku_stack_guard_start;
extern uint32_t __flash_start;    /* MRAM code window base */
extern uint32_t __flash_end;      /* MRAM code window end, below the mirror */

/**
 * @defgroup MPU_SSRAM_MAP Shared SRAM region constants
 * @brief Base address and size of the 3 MB shared SRAM block.
 *
 * Covered in its entirety as RW + XN so no byte of tier-buffer or
 * snapshot memory can ever be executed.
 * @{
 */
#define AMBIQ_SSRAM_BASE   0x20080000UL
#define AMBIQ_SSRAM_SIZE   (3UL * 1024UL * 1024UL)
/** @} */

/*---------------------------------------------------------------------------*/
/* MSP430-STYLE REGISTER COPIES                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Software mirror of the MSP430 MPUCTL0 register
 *
 * Kept for portable-test parity. The real ARMv8-M MPU is configured via CMSIS
 * ARM_MPU_* helpers; this stub mirrors the password-write and enable-bit
 * semantics so higher-level tests can read back a recognizable value.
 */
static uint16_t stub_mpuctl0;


/** @brief Software mirror of the MSP430 MPUSAM (segment access map) register */
static uint16_t stub_mpusam = TIKU_MPU_DEFAULT_SAM;

/** @brief Software mirror of MPUSEGB1 (segment 1/2 boundary) */
static uint16_t stub_mpusegb1;

/** @brief Software mirror of MPUSEGB2 (segment 2/3 boundary) */
static uint16_t stub_mpusegb2;

/*---------------------------------------------------------------------------*/
/* PERSISTENT DIAGNOSTIC STATE                                               */
/*---------------------------------------------------------------------------*/

/** @brief Magic value marking a valid mpu_diag block  */
#define TIKU_MPU_DIAG_MAGIC  0x4D505551U

/**
 * @brief Warm-reset-durable MPU diagnostic record
 *
 * Lives in the NOLOAD .mpu_diag section (DTCM, outside .uninit), so it is
 * writable while .uninit is read-only and is not mirrored to MRAM.  Preserved
 * across warm resets; the magic sentinel detects cold boot.
 */
struct tiku_mpu_diag {
    uint32_t magic;            /**< TIKU_MPU_DIAG_MAGIC when valid          */
    uint32_t violation_count;  /**< faults since the last cold boot         */
    uint32_t last_fault_addr;  /**< MMFAR at the last fault                 */
    uint32_t last_fault_cfsr;  /**< full CFSR at the last fault             */
    uint32_t last_fault_hfsr;  /**< HFSR at last fault (bit 30 = escalated) */
    uint32_t last_fault_ipsr;  /**< IPSR (handling exception number)        */
    uint32_t expect_fault;     /**< test scaffold: 1 armed, 2/3 observed    */
    uint32_t last_fault_pc;    /**< stacked PC at the last fault, or 0      */
    uint32_t last_fault_lr;    /**< stacked LR at the last fault, or 0      */
    uint32_t violation_flags;  /**< MMFSR flags retained across warm reset. */
};

/** @brief Warm-durable diagnostic block instance in .mpu_diag */
__attribute__((section(".mpu_diag")))
static volatile struct tiku_mpu_diag mpu_diag;

/*---------------------------------------------------------------------------*/
/* REGION MAP AND HARDWARE HELPERS                                           */
/*---------------------------------------------------------------------------*/

/**
 * @defgroup MPU_REGIONS ARMv8-M MPU region index constants
 * @brief Slot numbers for the eight non-overlapping DTCM/MRAM/SSRAM/ITCM
 *        regions.
 * @{
 */
#define MPU_REGION_NVM         0U
#define MPU_REGION_TEXT        1U
#define MPU_REGION_SRAM_LO     2U
#define MPU_REGION_SRAM_MID    3U
#define MPU_REGION_STACK_GUARD 4U
#define MPU_REGION_SRAM_TOP    5U
#define MPU_REGION_SSRAM       6U
#define MPU_REGION_MODULE      7U
/** @} */

/* The linker owns the 32K stack and 4K guard boundaries. */

/** @brief CFSR/MMFSR bit 7: MMFAR holds a valid fault address */
#define TIKU_MMFSR_MMARVALID       (1UL << 7)   /* CFSR/MMFSR: MMFAR valid */

/**
 * @brief MAIR0 attribute for Normal WB/WA cacheable memory (AttrIndx 0)
 *
 * Inner + outer Write-Back, non-transient, read+write allocate. Keeps
 * the M55 L1 cache active for MRAM and SSRAM. TCM bypasses the cache
 * regardless, so the attribute is harmless there.
 */
#define MPU_ATTR_NORMAL_WBWA \
    ARM_MPU_ATTR(ARM_MPU_ATTR_MEMORY_(1U, 1U, 1U, 1U), \
                 ARM_MPU_ATTR_MEMORY_(1U, 1U, 1U, 1U))

/**
 * @brief Program one ARMv8-M MPU region via CMSIS helpers
 *
 * Sets RBAR and RLAR for region @p rnr. Access policy: privileged +
 * unprivileged (NP=1), non-shareable, AttrIndx 0 (Normal WB/WA). The DSB + ISB
 * pair after the write makes the new attributes visible to the next access.
 *
 * @param rnr         Region number (0-15; Apollo510 M55 has 16)
 * @param base        Region base address (must be 32-byte aligned)
 * @param limit_incl  Last byte address inside the region (inclusive)
 * @param ro          1 = read-only, 0 = read-write
 * @param xn          1 = execute-never, 0 = executable
 */
static void mpu_region(uint32_t rnr, uint32_t base, uint32_t limit_incl,
                       uint32_t ro, uint32_t xn) {
    if (limit_incl < base) {
        ARM_MPU_ClrRegion(rnr);
        __DSB();
        __ISB();
        return;
    }
    ARM_MPU_SetRegion(rnr,
        ARM_MPU_RBAR(base, ARM_MPU_SH_NON, ro, 1U, xn),
        ARM_MPU_RLAR(limit_incl, 0U));
    __DSB();
    __ISB();
}

/**
 * @brief Apply region 0: .uninit (DTCM), read-only by default + XN
 *
 * Write protection for the persist cells, as on RP2350 and in the MSP430 SAM:
 * the region is RO outside tiku_mpu_unlock_nvm()/lock_nvm() windows and RW
 * only inside them, XN throughout (W^X).
 *
 * @param ro  1 = locked (default), 0 = inside an unlock window
 */
static void mpu_set_nvm_ap(uint32_t ro) {
    mpu_region(MPU_REGION_NVM,
               (uint32_t)(uintptr_t)&__uninit_start,
               (uint32_t)(uintptr_t)&__uninit_end - 1U,
               ro, 1U /* XN */);
}

/** @brief Arm region 0 (.uninit) read-only, its default state. */
static void mpu_set_nvm(void) {
    mpu_set_nvm_ap(1U /* RO: locked by default */);
}

/**
 * @brief Compute the base address of the 4 KB stack-guard region
 *
 * Uses the same linker boundary that stops TCM allocations.
 *
 * @return Base address of the stack-guard region
 */
static inline uint32_t mpu_stack_guard_base(void) {
    return (uint32_t)(uintptr_t)&__tiku_stack_guard_start;
}

/**
 * Stack-paint floor for /sys/mem/stack_free (kernel/cpu/tiku_stack): the first
 * byte above the guard region, from the same base the guard is armed with, so
 * painting up to the SP stays strictly inside the live-stack window.
 */
uint32_t tiku_stack_arch_bottom(void) {
    return (uint32_t)(uintptr_t)&__tiku_stack_bottom;
}

/*---------------------------------------------------------------------------*/
/* SAM AND CTL BOOKKEEPING                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the current software SAM (segment access map) value
 *
 * @return Current stub_mpusam value
 */
uint16_t tiku_mpu_arch_get_sam(void) { return stub_mpusam; }

/**
 * @brief Update the software SAM and mirror the MSP430 MPUCTL0 write sequence
 *
 * Bookkeeping kept for test parity: no MPU region changes here.  Region 0
 * (.uninit) follows unlock_nvm()/lock_nvm(), region 7 (module) follows
 * tiku_mpu_arch_module_window_exec(), and regions 1-6 are fixed at init.
 *
 * @param sam  New SAM value to store
 */
void tiku_mpu_arch_set_sam(uint16_t sam) {
    stub_mpuctl0 = 0xA500U;             /* mirror MSP430 password write */
    stub_mpusam  = sam;
    stub_mpuctl0 = 0xA500U | 0x0001U;   /* password | enable */
    /* Bookkeeping only: the code and data regions keep their init setting
     * whatever the SAM bits say (flash can't take stores, SRAM stays XN). */
}

/**
 * @brief Return the current software MPUCTL0 value
 *
 * @return Current stub_mpuctl0 value
 */
uint16_t tiku_mpu_arch_get_ctl(void) { return stub_mpuctl0; }

/**
 * @brief Disable all interrupts (PRIMASK wrapper)
 */
void tiku_mpu_arch_disable_irq(void) { tiku_cpu_irq_disable(); }

/**
 * @brief Enable all interrupts (PRIMASK wrapper)
 */
void tiku_mpu_arch_enable_irq(void)  { tiku_cpu_irq_enable(); }

/**
 * @brief Initialize all eight ARMv8-M MPU regions and enable the MPU
 *
 * Cold boot (magic absent) zeroes the .mpu_diag block and stamps the magic; a
 * warm reset preserves the violation counters.  Then programs MAIR0[0] with
 * Normal WB/WA, the eight regions in the table above, and MemManage at 0.
 *
 * @note PRIVDEFENA is on, so peripherals and the SCS keep default privileged
 *       access without using region slots.  HFNMIENA is off unless
 *       TIKU_MPU_HFNMI_ENFORCE=1.
 */
void tiku_mpu_arch_init_segments(void) {
    /* Cold-boot detect: zero + magic on first power-up; keep counters across
     * a warm (post-fault) reset so the violation survives to be read. */
    if (mpu_diag.magic != TIKU_MPU_DIAG_MAGIC) {
        mpu_diag.violation_flags = 0U;
        mpu_diag.magic            = TIKU_MPU_DIAG_MAGIC;
        mpu_diag.violation_count  = 0U;
        mpu_diag.last_fault_addr  = 0U;
        mpu_diag.last_fault_cfsr  = 0U;
        mpu_diag.last_fault_hfsr  = 0U;
        mpu_diag.last_fault_ipsr  = 0U;
        mpu_diag.expect_fault     = 0U;
        mpu_diag.last_fault_pc    = 0U;
        mpu_diag.last_fault_lr    = 0U;
    }

    /* MSP430-style SEGB values for code that reads them. */
    stub_mpusegb1 = 0x0800U;
    stub_mpusegb2 = 0x0C00U;

    ARM_MPU_Disable();                  /* DSB/ISB inside */
    ARM_MPU_SetMemAttr(0U, MPU_ATTR_NORMAL_WBWA);

    {
        uint32_t guard = mpu_stack_guard_base();

        mpu_set_nvm();                                    /* region 0: RO+XN */
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
                   guard, (uint32_t)(uintptr_t)&__tiku_stack_bottom - 1U,
                   1U /* RO */, 1U /* XN */);                   /* region 4 */
        mpu_region(MPU_REGION_SRAM_TOP,
                   (uint32_t)(uintptr_t)&__tiku_stack_bottom,
                   (uint32_t)(uintptr_t)&__sram_end - 1U,
                   0U, 1U);                                     /* region 5 */
        mpu_region(MPU_REGION_SSRAM,
                   AMBIQ_SSRAM_BASE,
                   AMBIQ_SSRAM_BASE + AMBIQ_SSRAM_SIZE - 1U,
                   0U, 1U);                                     /* region 6 */
        /* Region 7 covers wherever a loadable BASIC module executes, which
         * differs by part -- see kernel/shell/basic/tiku_basic_module.h.
         *
         * XIP parts: the MRAM slot directly above the code window, RO +
         * executable permanently: the module runs in place and the CPU never
         * stores to it (installs go through the bootrom programmer, which the
         * MPU does not gate).
         *
         * apollo510: the module is copied into an ITCM window and run there.
         * The region covers that window and starts in the resting state, RW +
         * XN; tiku_mpu_arch_module_window_exec() flips it to RO + X around the
         * branch, so the window is never writable and executable at once. */
#if TIKU_MODULE_EXEC_IN_RAM
        mpu_region(MPU_REGION_MODULE,
                   TIKU_MODULE_EXEC_ADDR,
                   TIKU_MODULE_EXEC_ADDR + TIKU_MODULE_CARVE_SIZE - 1U,
                   0U /* RW */, 1U /* XN */);                   /* region 7 */
#else
        /* The slot address and size come from the same constants the loader
         * installs with and the module links against (tiku_basic_module.h),
         * so the region cannot drift from them; the loader checks that pair
         * against the linker's __tiku_code_limit before any install. */
        mpu_region(MPU_REGION_MODULE,
                   TIKU_MODULE_CARVE_ADDR,
                   TIKU_MODULE_CARVE_ADDR + TIKU_MODULE_CARVE_SIZE - 1U,
                   1U /* RO */, 0U /* exec OK */);              /* region 7 */
#endif
    }

    /* Enable MPU + PRIVDEFENA.  HFNMIENA stays off unless
     * TIKU_MPU_HFNMI_ENFORCE=1: with it off the MPU is bypassed in HardFault
     * and NMI handlers, so a handler that strays does not lock the core up.
     * ARM_MPU_Enable ORs in ENABLE + DSB/ISB. */
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

/**
 * @brief Restore the SAM to the default (read+exec, no write) policy
 *
 * Delegates to tiku_mpu_arch_set_sam() with TIKU_MPU_DEFAULT_SAM: bookkeeping
 * only (see tiku_mpu_arch_set_sam()).
 */
void tiku_mpu_arch_set_default_protection(void) {
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

/**
 * @brief Set the permission bits for one software SAM segment
 *
 * Updates the 3-bit permission field for segment @p seg in stub_mpusam
 * (each segment occupies 4 bits; bits [2:0] of the field are the
 * permission flags).  Bookkeeping only.
 *
 * @param seg   Segment index (0-based)
 * @param perm  New 3-bit permission value (TIKU_MPU_READ/WRITE/EXEC)
 */
void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm) {
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t mask  = (uint16_t)0x07U << shift;
    uint16_t sam   = (uint16_t)((stub_mpusam & ~mask) |
                                (((uint16_t)perm & 0x07U) << shift));
    tiku_mpu_arch_set_sam(sam);
}

/**
 * @brief Unlock NVM for writing
 *
 * Makes region 0 (.uninit) RW for the window and ORs the W bits into the SAM,
 * as the MSP430 path does.  The generic tiku_mpu_lock_nvm() that closes the
 * window drives tiku_mem_arch_nvm_flush_status().
 *
 * @return Saved SAM value to pass to tiku_mpu_arch_lock_nvm()
 */
uint16_t tiku_mpu_arch_unlock_nvm(void) {
    /* Open the window: SAM bookkeeping for MSP430 parity, and region 0
     * (.uninit) goes RW for the duration.  The matching generic
     * tiku_mpu_lock_nvm() drives tiku_mem_arch_nvm_flush_status(), which
     * commits the MRAM mirror. */
    uint16_t saved = stub_mpusam;
    stub_mpusam = (uint16_t)(saved | 0x0222U);
    mpu_set_nvm_ap(0U /* RW */);
    return saved;
}

/**
 * @brief Restore the SAM after an NVM write window
 *
 * Delegates to tiku_mpu_arch_set_sam() with the value from
 * tiku_mpu_arch_unlock_nvm() and re-arms region 0 RO unless that value has
 * write bits.  tiku_mpu_lock_nvm_status() commits to MRAM before calling it.
 *
 * @param saved_state  Value returned by a prior tiku_mpu_arch_unlock_nvm()
 */
void tiku_mpu_arch_lock_nvm(uint16_t saved_state) {
    tiku_mpu_arch_set_sam(saved_state);
    /* Nest-safe: restore the AP the saved state implies.  An inner
     * lock inside a still-open outer window restores "unlocked" SAM
     * (write bits set) and must leave the region RW; the outermost
     * lock restores a no-write SAM and re-arms RO. */
    mpu_set_nvm_ap(((saved_state & 0x0222U) == 0U) ? 1U : 0U);
}

/**
 * @brief Read the warm-durable fault record (count, MMFAR, CFSR).
 *
 * The MemManage handler records into .mpu_diag and resets; after the reset
 * this record holds the previous fault.  Any output pointer may be NULL.
 */
void tiku_mpu_arch_diag_read(uint32_t *count, uint32_t *addr,
                             uint32_t *cfsr) {
    if (count != (void *)0) { *count = mpu_diag.violation_count; }
    if (addr  != (void *)0) { *addr  = mpu_diag.last_fault_addr; }
    if (cfsr  != (void *)0) { *cfsr  = mpu_diag.last_fault_cfsr; }
}

/**
 * @brief Read back whether MPU region 0 (.uninit) is currently RO.
 *
 * Test and diagnostic hook: decodes the live RLAR.EN and RBAR.AP fields of
 * region 0.
 *
 * @return 1 when the region is read-only, 0 when writable or disabled
 */
uint8_t tiku_mpu_arch_nvm_region_ro(void) {
    MPU->RNR = MPU_REGION_NVM;
    __DSB();
    /* A disabled region (RLAR.EN, bit 0, clear) protects nothing and reads
     * as 0, as in the Apollo4 reader.  An enabled one is read-only, at either
     * privilege, when RBAR.AP[2:1] has its upper bit (RBAR bit 2) set. */
    if ((MPU->RLAR & MPU_RLAR_EN_Msk) == 0U) {
        return 0u;
    }
    return ((MPU->RBAR & (2U << 1)) != 0U) ? 1u : 0u;
}

/**
 * @brief Return the current software violation flags (mpu_diag.violation_flags)
 *
 * @return MMFSR bits ORed in by the MemManage handler
 */
uint16_t tiku_mpu_arch_get_violation_flags(void)   { return mpu_diag.violation_flags; }

/**
 * @brief Clear all software violation flags
 */
void     tiku_mpu_arch_clear_violation_flags(void) { mpu_diag.violation_flags = 0U; }

/**
 * @brief Enable MemManage and set the MSP430 SEGIE bit in stub_mpuctl0
 *
 * Ensures SCB->SHCSR has MEMFAULTENA set, so an MPU fault enters
 * tiku_ambiq_mem_fault_handler instead of escalating to HardFault.  That
 * handler records the fault and resets.
 */
void tiku_mpu_arch_enable_violation_nmi(void) {
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
    __DSB();
    __ISB();
    stub_mpuctl0 |= 0x0010U;            /* mirror MPU_SEGIE */
}

/*---------------------------------------------------------------------------*/
/* DIAGNOSTICS AND VIOLATION-TEST HOOKS                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the warm-durable MPU violation count
 *
 * @return MemManage and HardFault events since the last cold boot or
 *         tiku_mpu_arch_test_clear_violation()
 */
uint32_t tiku_mpu_arch_violation_count(void)  { return mpu_diag.violation_count; }

/**
 * @brief Return the MMFAR address captured at the last MPU fault
 *
 * @return MMFAR as the last HardFault, or MemManage fault with MMARVALID set,
 *         recorded it; 0 after a cold boot or
 *         tiku_mpu_arch_test_clear_violation()
 */
uint32_t tiku_mpu_arch_last_fault_addr(void)  { return mpu_diag.last_fault_addr; }

/**
 * @brief Return the CFSR value captured at the last MPU fault
 *
 * @return Last CFSR snapshot
 */
uint32_t tiku_mpu_arch_last_fault_cfsr(void)  { return mpu_diag.last_fault_cfsr; }

/**
 * @brief Return the expect_fault test-scaffold field
 *
 * @return 0 unarmed, 1 armed, 2 MemManage observed, 3 HardFault observed
 */
uint32_t tiku_mpu_arch_test_expect_fault(void){ return mpu_diag.expect_fault; }

/**
 * @brief Arm the test scaffold to expect one MPU fault
 *
 * Sets expect_fault to 1; the MemManage handler moves it to 2 and the
 * HardFault handler to 3 when the fault arrives.
 */
void     tiku_mpu_arch_test_arm_fault(void)   { mpu_diag.expect_fault = 1U; }

/**
 * @brief Clear the test-scaffold violation state for a clean next test
 *
 * Zeroes violation_count, last_fault_addr and expect_fault in the warm-durable
 * diagnostic block.  last_fault_cfsr and last_fault_hfsr are left alone so the
 * fault type stays inspectable after the test.
 */
void tiku_mpu_arch_test_clear_violation(void) {
    mpu_diag.violation_count = 0U;
    mpu_diag.last_fault_addr = 0U;
    mpu_diag.expect_fault    = 0U;
}

/*---------------------------------------------------------------------------*/
/* FAULT HANDLERS                                                            */
/*---------------------------------------------------------------------------*/

/*
 * Strong overrides of the weak crt_early aliases.  Each handler is a naked
 * shim that (1) disables the MPU and (2) resolves the stacked exception frame
 * before any stack is used, then tail-branches into a C body that records and
 * dumps the fault and resets.
 *
 * The MPU goes off first because a stack overflow into the RO stack-guard
 * region (region 4) faults on the overflowed stack: the handler's own prologue
 * pushes would re-fault into the guard, MemManage would escalate to HardFault,
 * HardFault's pushes would re-fault again, and the core would lock up
 * silently.  Both handlers end in SystemReset, so protection stays off only
 * for their few hundred instructions.
 */

/* Fault-time console dump: .mpu_diag can be read only after the reset, so the
 * handler also prints the fault on the UART first.  It uses the bounded
 * fault-path putc of tiku_uart_arch.c: no printf, and no TX wait that could
 * hang the handler. */
extern void tiku_uart_fault_putc(char c);
extern void tiku_uart_fault_drain(void);

/** CFSR stacking-error bits: MMFSR.MSTKERR (bit 4) | BFSR.STKERR (bit 12).
 *  When either is set the exception frame push itself failed, so the
 *  stacked PC/LR words are unreliable and are recorded as 0 instead. */
#define TIKU_CFSR_STKERR_MASK  ((1UL << 4) | (1UL << 12))

/** @brief Print @p v as 8 hex digits through the fault-path UART putc. */
static void fault_puthex(uint32_t v) {
    static const char hx[] = "0123456789abcdef";
    int i;
    for (i = 28; i >= 0; i -= 4) {
        tiku_uart_fault_putc(hx[(v >> i) & 0xFU]);
    }
}

/** @brief Print one [TM:FAULT] line on the fault-path UART and drain it. */
static void fault_dump(const char *tag, uint32_t cfsr, uint32_t addr,
                       uint32_t pc, uint32_t lr) {
    const char *p;
    for (p = "\r\n[TM:FAULT] "; *p; p++) { tiku_uart_fault_putc(*p); }
    for (p = tag; *p; p++)                { tiku_uart_fault_putc(*p); }
    for (p = " cfsr=0x"; *p; p++)         { tiku_uart_fault_putc(*p); }
    fault_puthex(cfsr);
    for (p = " addr=0x"; *p; p++)         { tiku_uart_fault_putc(*p); }
    fault_puthex(addr);
    for (p = " pc=0x"; *p; p++)           { tiku_uart_fault_putc(*p); }
    fault_puthex(pc);
    for (p = " lr=0x"; *p; p++)           { tiku_uart_fault_putc(*p); }
    fault_puthex(lr);
    tiku_uart_fault_putc('\r');
    tiku_uart_fault_putc('\n');
    /* putc returns on FIFO room, not FIFO empty: without a drain the
     * SystemReset that follows destroys up to 32 still-queued characters
     * and the host sees a truncated (or empty) dump. */
    tiku_uart_fault_drain();
}

/**
 * @brief Record a fault into mpu_diag, capture the stacked PC/LR, dump, reset
 *
 * Shared tail of both fault bodies. @p frame points at the hardware exception
 * frame ([0..3]=r0-r3, [4]=r12, [5]=lr, [6]=pc, [7]=xpsr) on whichever stack
 * EXC_RETURN selected; it is trusted only when CFSR reports no stacking error.
 */
static void fault_record_and_reset(const char *tag, uint32_t cfsr,
                                   const uint32_t *frame) {
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(ipsr));

    mpu_diag.last_fault_cfsr = cfsr;
    mpu_diag.last_fault_hfsr = SCB->HFSR;
    mpu_diag.last_fault_ipsr = ipsr;
    if ((cfsr & TIKU_CFSR_STKERR_MASK) == 0U && frame != (const uint32_t *)0) {
        mpu_diag.last_fault_lr = frame[5];
        mpu_diag.last_fault_pc = frame[6];
    } else {
        mpu_diag.last_fault_lr = 0U;
        mpu_diag.last_fault_pc = 0U;
    }
    mpu_diag.violation_count++;

    fault_dump(tag, cfsr, mpu_diag.last_fault_addr,
               mpu_diag.last_fault_pc, mpu_diag.last_fault_lr);

    /* A synchronous access violation cannot be stepped over, so the handler
     * resets; the incremented .mpu_diag counter is read after the reset. */
    __DSB();
    NVIC_SystemReset();
    for (;;) { }
}

/**
 * @brief MemManage fault C body (jumped to by the naked shim)
 *
 * Captures CFSR, MMFAR (when MMARVALID), the stacked PC/LR, HFSR and IPSR
 * into mpu_diag, counts the violation, moves expect_fault from 1 (armed) to 2
 * (observed), clears CFSR, dumps and resets.
 *
 * @param frame  Stacked exception frame (MSP or PSP per EXC_RETURN)
 */
__attribute__((used))
static void ambiq_mem_fault_body(const uint32_t *frame) {
    uint32_t cfsr  = SCB->CFSR;
    uint32_t mmfsr = cfsr & 0xFFU;

    if (mmfsr & TIKU_MMFSR_MMARVALID) {
        mpu_diag.last_fault_addr = SCB->MMFAR;
    }
    mpu_diag.violation_flags |= (uint16_t)mmfsr;
    if (mpu_diag.expect_fault == 1U) {
        mpu_diag.expect_fault = 2U;     /* observed */
    }

    SCB->CFSR = cfsr;                   /* W1C */

    fault_record_and_reset("memmanage", cfsr, frame);
}

/**
 * @brief HardFault C body (jumped to by the naked shim)
 *
 * Handles every HardFault, including an MPU fault MemManage could not take
 * (raised with PRIMASK set or at priority 0, or before MemManage is enabled).
 * Records the same fields, moves expect_fault to 3, dumps and resets.
 *
 * @param frame  Stacked exception frame (MSP or PSP per EXC_RETURN)
 */
__attribute__((used))
static void ambiq_hard_fault_body(const uint32_t *frame) {
    uint32_t cfsr = SCB->CFSR;

    mpu_diag.last_fault_addr = SCB->MMFAR;
    if (mpu_diag.expect_fault == 1U) {
        mpu_diag.expect_fault = 3U;     /* HardFault path observed */
    }

    fault_record_and_reset("hardfault", cfsr, frame);
}

/*
 * Naked entry shims. No C prologue may run before the MPU is off: if the
 * original fault was a stack overflow into the RO guard, the first push
 * would re-fault. movw/movt build the MPU->CTRL address with no load from
 * memory. EXC_RETURN bit 2 selects which stack holds the exception frame.
 */

/** @brief MemManage handler; overrides the weak crt_early alias. */
__attribute__((naked))
void tiku_ambiq_mem_fault_handler(void) {
    __asm__ volatile (
        "movw  r0, #0xED94            \n\t"   /* MPU->CTRL (0xE000ED94)   */
        "movt  r0, #0xE000            \n\t"
        "movs  r1, #0                 \n\t"
        "str   r1, [r0]               \n\t"   /* MPU off: no re-faults    */
        "dsb                          \n\t"
        "isb                          \n\t"
        "tst   lr, #4                 \n\t"   /* EXC_RETURN.SPSEL         */
        "ite   eq                     \n\t"
        "mrseq r0, msp                \n\t"
        "mrsne r0, psp                \n\t"
        "b     ambiq_mem_fault_body   \n\t"
    );
}

/** @brief HardFault handler; overrides the weak crt_early alias. */
__attribute__((naked))
void tiku_ambiq_hard_fault_handler(void) {
    __asm__ volatile (
        "movw  r0, #0xED94            \n\t"   /* MPU->CTRL (0xE000ED94)   */
        "movt  r0, #0xE000            \n\t"
        "movs  r1, #0                 \n\t"
        "str   r1, [r0]               \n\t"   /* MPU off: no re-faults    */
        "dsb                          \n\t"
        "isb                          \n\t"
        "tst   lr, #4                 \n\t"   /* EXC_RETURN.SPSEL         */
        "ite   eq                     \n\t"
        "mrseq r0, msp                \n\t"
        "mrsne r0, psp                \n\t"
        "b     ambiq_hard_fault_body  \n\t"
    );
}

/**
 * @brief Flip the module execution window between writable and executable.
 *
 * The module loader calls it so the window is never writable and executable
 * at once (contract in tiku_mem.h).  mpu_region() issues the DSB/ISB pair, so
 * it holds for the next fetch, which may be the module.  No-op for XIP.
 *
 * @param enable  1 = RO + executable (a module is about to run / is running),
 *                0 = RW + execute-never (resting; the loader may write).
 */
void tiku_mpu_arch_module_window_exec(int enable)
{
#if TIKU_MODULE_EXEC_IN_RAM
    mpu_region(MPU_REGION_MODULE,
               TIKU_MODULE_EXEC_ADDR,
               TIKU_MODULE_EXEC_ADDR + TIKU_MODULE_CARVE_SIZE - 1U,
               enable ? 1U : 0U,        /* RO while executable */
               enable ? 0U : 1U);       /* XN while writable   */
#else
    (void)enable;                       /* XIP: no window to flip */
#endif
}
