/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_apollo4l.c - Apollo4 Lite (Cortex-M4) MPU driver, ARMv7-M W^X.
 *
 * The M4 has the PMSAv7 MPU: eight power-of-two regions set through RBAR and
 * RASR.  The HAL surface and fault behaviour are those of the Apollo510 driver;
 * the region map is below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "apollo4l.h"            /* CMSIS: MPU/SCB/NVIC + mpu_armv7.h */
#include <hal/tiku_cpu.h>        /* tiku_cpu_irq_disable/enable */
#include <stdint.h>

/*
 * W^X map (PMSAv7 -- the highest-numbered overlapping region wins):
 *   0  CODE   MRAM 0x0 + 2 MB              RO + exec  (code, rodata, SBL,
 *                                                      module slot)
 *   1  RAM    TCM/SRAM 0x10000000 + 2 MB   RW + XN    (.data/.bss/stack/
 *                                                      .ssram/tier)
 *   2  GUARD  4 KB below the 32 KB stack   no access  (stack-overflow trip)
 *   3  NVM    .uninit, 8 KB                RO + XN    (RW in an unlock window)
 * PRIVDEFENA covers peripherals (0x40000000+), the SCS (0xE0000000+) and the
 * bootrom with the default privileged policy.  MemManage is enabled at
 * priority 0; a violation records into the warm-durable .mpu_diag and resets.
 * A stack overflow trips the guard before it can corrupt .data/.bss/.uninit,
 * which end far below the guard.  Region 1 is 4 MB on the Apollo4 Plus.
 */

/*---------------------------------------------------------------------------*/
/* REGION CONSTANTS                                                          */
/*---------------------------------------------------------------------------*/

#define MPU_REGION_CODE         0U
#define MPU_REGION_RAM          1U
#define MPU_REGION_STACK_GUARD  2U
#define MPU_REGION_NVM          3U   /* .uninit: RO outside NVM windows */

/** MRAM (code) base + a 2 MB region covering the whole 2 MB MRAM. */
#define MPU_CODE_BASE     0x00000000UL
/** TCM (0x10000000, 384K) + shared SRAM (0x10060000): 2 MB covers the Lite's
 *  1 MB SSRAM; the Plus maps 2 MB SSRAM (to 0x10260000) and needs the next
 *  PMSAv7 power of two, 4 MB.  The slack above physical SSRAM is never used. */
#define MPU_RAM_BASE      0x10000000UL
#if defined(TIKU_DEVICE_APOLLO4P)
#define MPU_RAM_SIZE      ARM_MPU_REGION_SIZE_4MB
#else
#define MPU_RAM_SIZE      ARM_MPU_REGION_SIZE_2MB
#endif

/* Linker-owned 32K stack and 4K guard; the script asserts 4K alignment. */
extern uint32_t __tiku_stack_bottom;
extern uint32_t __tiku_stack_guard_start;

/**
 * Stack-paint floor for /sys/mem/stack_free (kernel/cpu/tiku_stack): the first
 * byte above the no-access guard region, so painting up to the SP stays
 * strictly inside the live-stack window.
 */
uint32_t tiku_stack_arch_bottom(void)
{
    return (uint32_t)(uintptr_t)&__tiku_stack_bottom;
}

/*---------------------------------------------------------------------------*/
/* MSP430-STYLE REGISTER COPIES                                              */
/*---------------------------------------------------------------------------*/

static uint16_t stub_mpuctl0;
static uint16_t stub_mpusam = TIKU_MPU_DEFAULT_SAM;
static uint16_t stub_mpusegb1;
static uint16_t stub_mpusegb2;

/*---------------------------------------------------------------------------*/
/* PERSISTENT DIAGNOSTIC STATE                                               */
/*---------------------------------------------------------------------------*/

#define TIKU_MPU_DIAG_MAGIC  0x4D505551U

/** @brief Fault record in the NOLOAD .mpu_diag section; survives warm reset. */
struct tiku_mpu_diag {
    uint32_t magic;
    uint32_t violation_count;
    uint32_t last_fault_addr;
    uint32_t last_fault_cfsr;
    uint32_t last_fault_hfsr;
    uint32_t last_fault_ipsr;
    uint32_t expect_fault;
    uint32_t last_fault_pc;    /* stacked PC at the last fault (0 if unknown) */
    uint32_t last_fault_lr;    /* stacked LR at the last fault (0 if unknown) */
    uint32_t violation_flags;  /**< MMFSR flags retained across warm reset. */
};

__attribute__((section(".mpu_diag")))
static volatile struct tiku_mpu_diag mpu_diag;

/** CFSR/MMFSR bit 7: MMFAR holds a valid fault address */
#define TIKU_MMFSR_MMARVALID  (1UL << 7)

/*---------------------------------------------------------------------------*/
/* SAM AND CTL BOOKKEEPING                                                   */
/*---------------------------------------------------------------------------*/

uint16_t tiku_mpu_arch_get_sam(void) { return stub_mpusam; }

void tiku_mpu_arch_set_sam(uint16_t sam) {
    stub_mpuctl0 = 0xA500U;             /* mirror MSP430 password write */
    stub_mpusam  = sam;
    stub_mpuctl0 = 0xA500U | 0x0001U;   /* password | enable */
    /* Bookkeeping only: no MPU region changes.  Region 3 (.uninit) follows
     * unlock_nvm()/lock_nvm(); the other regions are fixed at init. */
}

uint16_t tiku_mpu_arch_get_ctl(void) { return stub_mpuctl0; }

void tiku_mpu_arch_disable_irq(void) { tiku_cpu_irq_disable(); }
void tiku_mpu_arch_enable_irq(void)  { tiku_cpu_irq_enable(); }


/** Linker symbol: base of the 8 KB .uninit MPU envelope (8K-aligned). */
extern uint32_t __uninit_start[];

/**
 * @brief Program region 3 (.uninit, 8 KB) as RO or RW + XN.
 *
 * Write protection for the persist cells on the M4 (PMSAv7): region 3 outranks
 * region 1's blanket RW (higher number wins), is read-only by default, and is
 * RW only inside tiku_mpu_unlock_nvm() / lock_nvm() windows.
 *
 * @param ro  1 = locked (default), 0 = inside an unlock window
 */
static void mpu_set_nvm_ap(uint32_t ro) {
    ARM_MPU_SetRegion(
        ARM_MPU_RBAR(MPU_REGION_NVM, (uint32_t)__uninit_start),
        ARM_MPU_RASR(1U /* XN */, ro ? ARM_MPU_AP_RO : ARM_MPU_AP_FULL,
                     1U, 0U, 0U, 0U, 0U, ARM_MPU_REGION_SIZE_8KB));
    __DSB();
    __ISB();
}
/**
 * @brief Initialize the PMSAv7 MPU (coarse W^X) and enable it.
 *
 * Cold boot zeroes and stamps .mpu_diag; a warm reset keeps the counters.
 * Programs regions 0-3 as in the map above, clears slots 4-7, enables the MPU
 * with PRIVDEFENA, and puts MemManage at priority 0.
 */
void tiku_mpu_arch_init_segments(void) {
    uint32_t r;

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

    stub_mpusegb1 = 0x0800U;
    stub_mpusegb2 = 0x0C00U;

    ARM_MPU_Disable();

    /* Region 0: 2 MB of MRAM at 0x0 -- read-only + executable (code/rodata).
     * Normal, non-cacheable (TEX=1,C=0,B=0); the core has no L1 cache.  The
     * Apollo4 CACHECTRL cache over MRAM is enabled at boot, outside the MPU
     * (tiku_cpu_freq_boot_apollo4l.c). */
    ARM_MPU_SetRegion(
        ARM_MPU_RBAR(MPU_REGION_CODE, MPU_CODE_BASE),
        ARM_MPU_RASR(0U /* exec */, ARM_MPU_AP_RO, 1U, 0U, 0U, 0U, 0U,
                     ARM_MPU_REGION_SIZE_2MB));

    /* Region 1: RW + execute-never at 0x10000000 (TCM + shared SRAM:
     * .data/.bss/stack/.ssram/tier buffers). 2 MB on the Lite (1 MB SSRAM),
     * 4 MB on the Plus (full 2 MB SSRAM) -- see MPU_RAM_SIZE. */
    ARM_MPU_SetRegion(
        ARM_MPU_RBAR(MPU_REGION_RAM, MPU_RAM_BASE),
        ARM_MPU_RASR(1U /* XN */, ARM_MPU_AP_FULL, 1U, 0U, 0U, 0U, 0U,
                     MPU_RAM_SIZE));

    /* Region 2: the 4 KB stack guard directly below the 32 KB stack -- no
     * access, execute-never. PMSAv7 gives the highest-numbered region
     * precedence on overlap, so this overrides region 1's RW for its 4 KB:
     * a stack that overflows past its budget faults here before it can reach
     * .data/.bss/.uninit far below. */
    {
        uint32_t guard = (uint32_t)(uintptr_t)&__tiku_stack_guard_start;
        ARM_MPU_SetRegion(
            ARM_MPU_RBAR(MPU_REGION_STACK_GUARD, guard),
            ARM_MPU_RASR(1U /* XN */, ARM_MPU_AP_NONE, 1U, 0U, 0U, 0U, 0U,
                         ARM_MPU_REGION_SIZE_4KB));
    }

    /* Region 3: .uninit (8 KB), read-only by default -- the persist cells'
     * write protection; RW only inside unlock_nvm()/lock_nvm() windows. */
    mpu_set_nvm_ap(1U /* RO: locked by default */);

    /* Clear the unused slots 4-7.  Region 3, the .uninit envelope armed just
     * above, stays out of this loop: with region 3 cleared, region 1's RW
     * applies to the persist cells until an NVM window re-arms it. */
    for (r = 4U; r < 8U; r++) {
        ARM_MPU_ClrRegion(r);
    }

    /* Enable MPU + PRIVDEFENA (peripherals / SCS / bootrom keep the default
     * privileged map without using region slots). */
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);

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

/**
 * @brief Unlock NVM for writing.
 *
 * ORs the write bits into the SAM and makes region 3 (.uninit) RW for the
 * window; the generic tiku_mpu_lock_nvm() closing it drives the MRAM flush.
 */
uint16_t tiku_mpu_arch_unlock_nvm(void) {
    uint16_t saved = stub_mpusam;
    stub_mpusam = (uint16_t)(saved | 0x0222U);
    mpu_set_nvm_ap(0U /* RW for the window */);
    return saved;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved_state) {
    tiku_mpu_arch_set_sam(saved_state);
    /* Nest-safe: restore the AP the saved state implies (an inner
     * lock inside a still-open outer window keeps the region RW). */
    mpu_set_nvm_ap(((saved_state & 0x0222U) == 0U) ? 1U : 0U);
}

/**
 * @brief Read back whether MPU region 3 (.uninit) is currently RO.
 *
 * Decodes the live RASR.ENABLE and RASR.AP fields of region 3, as the
 * Apollo510 reader does for its region 0.
 *
 * @return 1 when the region is enabled and its AP is not AP_FULL, else 0
 */
uint8_t tiku_mpu_arch_nvm_region_ro(void) {
    uint32_t rasr, ap;
    MPU->RNR = MPU_REGION_NVM;
    __DSB();
    rasr = MPU->RASR;
    /* A disabled region, such as a cleared region 3 (RASR = 0), protects
     * nothing and reports not-RO. */
    if ((rasr & MPU_RASR_ENABLE_Msk) == 0U) {
        return 0u;
    }
    ap = (rasr >> MPU_RASR_AP_Pos) & 0x7U;
    return (ap == ARM_MPU_AP_FULL) ? 0u : 1u;
}

uint16_t tiku_mpu_arch_get_violation_flags(void)   { return mpu_diag.violation_flags; }
void     tiku_mpu_arch_clear_violation_flags(void) { mpu_diag.violation_flags = 0U; }

void tiku_mpu_arch_enable_violation_nmi(void) {
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
    __DSB();
    __ISB();
    stub_mpuctl0 |= 0x0010U;            /* mirror MPU_SEGIE */
}

/*---------------------------------------------------------------------------*/
/* DIAGNOSTICS AND TEST HOOKS                                                */
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
/* FAULT HANDLERS                                                            */
/*---------------------------------------------------------------------------*/

/*
 * Strong overrides of the weak crt_early aliases, as in the Apollo510 driver;
 * the ARMv7-M SCB fault registers (CFSR/MMFAR/HFSR/SHCSR) match the M55's.
 * Each handler is a naked shim that (1) disables the MPU and (2) resolves the
 * stacked exception frame before any stack is used, then tail-branches into a
 * C body that records and dumps the fault and resets.
 *
 * The MPU goes off first because a stack overflow into the no-access
 * stack-guard region (region 2) faults on the overflowed stack: the handler's
 * own prologue pushes would re-fault into the guard, MemManage would escalate
 * to HardFault, HardFault's pushes would re-fault again, and the core would
 * lock up silently.  Both handlers end in SystemReset, so protection stays off
 * only for their few hundred instructions.
 */

/* Fault-time console dump, as on the Apollo510: .mpu_diag can be read only
 * after the reset, so the handler also prints the fault on the UART first.
 * It uses the bounded fault-path putc of tiku_uart_apollo4l.c: no printf,
 * and no TX wait that could hang it. */
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

    __DSB();
    NVIC_SystemReset();
    for (;;) { }
}

/**
 * @brief MemManage fault C body (jumped to by the naked shim)
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
 * original fault was a stack overflow into the no-access guard, the first
 * push would re-fault. movw/movt build the MPU->CTRL address with no load
 * from memory. EXC_RETURN bit 2 selects which stack holds the exception frame.
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

/** @brief Apollo4 modules execute in place from read-only MRAM. */
void tiku_mpu_arch_module_window_exec(int enable)
{
    (void)enable;
}
