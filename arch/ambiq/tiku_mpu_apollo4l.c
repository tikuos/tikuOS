/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_apollo4l.c - Apollo4 Lite (Cortex-M4) MPU driver -- ARMv7-M W^X
 *
 * The Cortex-M4 has the ARMv7-M PMSAv7 MPU (8 regions, RBAR + RASR, power-of-two
 * size-aligned regions) -- NOT the ARMv8-M MPU (RBAR + RLAR, arbitrary base/limit,
 * MAIR) used by the apollo510 driver. This is a from-scratch PMSAv7 version that
 * keeps the same HAL surface and fault-diagnostic behaviour but uses the vendored
 * mpu_armv7.h helpers.
 *
 * W^X map (PMSAv7 -- the highest-numbered overlapping region wins):
 *   0  CODE   MRAM 0x0 + 2 MB              RO + exec   (code + rodata; SBL is RO too)
 *   1  RAM    TCM/SRAM 0x10000000 + 2 MB   RW + XN     (.data/.bss/stack/.ssram/tier)
 *   2  GUARD  32 B, 32 KB below the stack  no access   (stack-overflow trip)
 * PRIVDEFENA covers peripherals (0x40000000+), the SCS (0xE0000000+) and the
 * bootrom with the default privileged policy. MemManage is enabled at priority 0;
 * a violation records into the warm-durable .mpu_diag and resets. Code is RO+X,
 * all data is execute-never, and a stack overflow trips the guard before it can
 * corrupt .data/.bss/.uninit (which end far below the guard).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "apollo4l.h"            /* CMSIS: MPU/SCB/NVIC + mpu_armv7.h (via core_cm4.h) */
#include <hal/tiku_cpu.h>        /* tiku_cpu_irq_disable/enable */
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Region constants (apollo4l memory map)                                    */
/*---------------------------------------------------------------------------*/

#define MPU_REGION_CODE         0U
#define MPU_REGION_RAM          1U
#define MPU_REGION_STACK_GUARD  2U
#define MPU_REGION_NVM          3U   /* .uninit: RO outside NVM windows */

/** MRAM (code) base + a 2 MB region covering the whole 2 MB MRAM. */
#define MPU_CODE_BASE     0x00000000UL
/** TCM(0x10000000,384K) + shared SRAM(0x10060000). The Lite maps 1 MB SSRAM, so
 *  a 2 MB region spans both; the Plus maps the full 2 MB SSRAM (ends 0x10260000)
 *  and needs the next PMSAv7 power-of-2 -- a 4 MB region (0x10000000..0x10400000,
 *  4 MB-aligned). The slack above physical SSRAM is never accessed. */
#define MPU_RAM_BASE      0x10000000UL
#if defined(TIKU_DEVICE_APOLLO4P)
#define MPU_RAM_SIZE      ARM_MPU_REGION_SIZE_4MB
#else
#define MPU_RAM_SIZE      ARM_MPU_REGION_SIZE_2MB
#endif

/** Stack guard: 32 bytes placed this far below the stack top (mirrors the
 *  apollo510 budget). The kernel's stack stays well within 32 KB. */
#define MPU_STACK_RESERVED_BYTES  32768U
#define MPU_STACK_GUARD_BYTES     32U

/** Top of TCM = stack base (apollo4l.ld); the guard sits below it. */
extern uint32_t __sram_end;

/*---------------------------------------------------------------------------*/
/* Software-bookkept MSP430-style register file (parity for portable tests)  */
/*---------------------------------------------------------------------------*/

static uint16_t stub_mpuctl0;
static uint16_t stub_mpuctl1;     /* violation flags */
static uint16_t stub_mpusam = TIKU_MPU_DEFAULT_SAM;
static uint16_t stub_mpusegb1;
static uint16_t stub_mpusegb2;

/*---------------------------------------------------------------------------*/
/* Persistent diagnostic state (.mpu_diag NOLOAD, warm-reset durable)        */
/*---------------------------------------------------------------------------*/

#define TIKU_MPU_DIAG_MAGIC  0x4D505550U   /* 'MPUP' */

struct tiku_mpu_diag {
    uint32_t magic;
    uint32_t violation_count;
    uint32_t last_fault_addr;
    uint32_t last_fault_cfsr;
    uint32_t last_fault_hfsr;
    uint32_t last_fault_ipsr;
    uint32_t expect_fault;
};

__attribute__((section(".mpu_diag")))
static volatile struct tiku_mpu_diag mpu_diag;

/** CFSR/MMFSR bit 7: MMFAR holds a valid fault address */
#define TIKU_MMFSR_MMARVALID  (1UL << 7)

/*---------------------------------------------------------------------------*/
/* SAM/CTL bookkeeping + HAL surface (device-agnostic, mirrors apollo510)     */
/*---------------------------------------------------------------------------*/

uint16_t tiku_mpu_arch_get_sam(void) { return stub_mpusam; }

void tiku_mpu_arch_set_sam(uint16_t sam) {
    stub_mpuctl0 = 0xA500U;             /* mirror MSP430 password write */
    stub_mpusam  = sam;
    stub_mpuctl0 = 0xA500U | 0x0001U;   /* password | enable */
    /* Bookkeeping only: .uninit stays RW+XN (the NVM tier pool shares it), and
     * the PMSAv7 regions are fixed at init time. */
}

uint16_t tiku_mpu_arch_get_ctl(void) { return stub_mpuctl0; }

void tiku_mpu_arch_disable_irq(void) { tiku_cpu_irq_disable(); }
void tiku_mpu_arch_enable_irq(void)  { tiku_cpu_irq_enable(); }


/** Linker symbol: base of the 8 KB .uninit MPU envelope (8K-aligned). */
extern uint32_t __uninit_start[];

/**
 * @brief Program region 3 (.uninit, 8 KB) as RO or RW + XN.
 *
 * REAL write protection for the persist cells on the M4 (PMSAv7):
 * region 3 outranks region 1's blanket RW (higher number wins), is
 * read-only by default, and flips RW only inside tiku_mpu_unlock_nvm()
 * / lock_nvm() windows -- the same contract as apollo510 and RP2350.
 * The linker guarantees the power-of-2 base/size (see apollo4l.ld).
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
 * Cold-boot: zero + stamp .mpu_diag; warm (post-fault) reset: keep counters.
 * Programs region 0 (MRAM RO+exec) and region 1 (TCM/SRAM RW+XN), clears the
 * remaining six region slots, enables the MPU with PRIVDEFENA, and routes
 * MemManage faults to priority 0.
 */
void tiku_mpu_arch_init_segments(void) {
    uint32_t r;

    if (mpu_diag.magic != TIKU_MPU_DIAG_MAGIC) {
        mpu_diag.magic            = TIKU_MPU_DIAG_MAGIC;
        mpu_diag.violation_count  = 0U;
        mpu_diag.last_fault_addr  = 0U;
        mpu_diag.last_fault_cfsr  = 0U;
        mpu_diag.last_fault_hfsr  = 0U;
        mpu_diag.last_fault_ipsr  = 0U;
        mpu_diag.expect_fault     = 0U;
    }

    stub_mpusegb1 = 0x0800U;
    stub_mpusegb2 = 0x0C00U;

    ARM_MPU_Disable();

    /* Region 0: 2 MB of MRAM at 0x0 -- read-only + executable (code/rodata).
     * Normal, non-cacheable (TEX=1,C=0,B=0); the core has no L1 cache and the
     * system cache is left off at bring-up. */
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

    /* Region 2: 32-byte stack guard, MPU_STACK_RESERVED_BYTES below the stack
     * top -- no access, execute-never. PMSAv7 gives the highest-numbered region
     * precedence on overlap, so this overrides region 1's RW for its 32 bytes:
     * a stack that overflows past its budget faults here before it can reach
     * .data/.bss/.uninit far below. */
    {
        uint32_t guard = (uint32_t)(uintptr_t)&__sram_end
                         - MPU_STACK_RESERVED_BYTES - MPU_STACK_GUARD_BYTES;
        ARM_MPU_SetRegion(
            ARM_MPU_RBAR(MPU_REGION_STACK_GUARD, guard),
            ARM_MPU_RASR(1U /* XN */, ARM_MPU_AP_NONE, 1U, 0U, 0U, 0U, 0U,
                         ARM_MPU_REGION_SIZE_32B));
    }

    /* Region 3: .uninit (8 KB) read-only by default -- the persist cells' real
     * W^X protection; flips RW only inside unlock_nvm()/lock_nvm() windows.
     * Armed AFTER regions 0-2 so the clear loop below cannot wipe it. */
    mpu_set_nvm_ap(1U /* RO: locked by default */);

    /* Clear the unused region slots 4-7 ONLY. Region 3 is the .uninit envelope
     * just armed; the old loop started at r=3 and disabled it one line later,
     * leaving cells writable from MPU-enable until the first NVM window happened
     * to re-arm region 3 -- a boot-window W^X gap. Start at 4 to keep it. */
    for (r = 4U; r < 8U; r++) {
        ARM_MPU_ClrRegion(r);
    }

    /* Enable MPU + PRIVDEFENA (peripherals / SCS / bootrom keep the default
     * privileged map without burning region slots). */
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
 * @brief Unlock NVM for writing (bookkeeping path).
 *
 * .uninit is already RW (region 1), so this is SAM bookkeeping only; the
 * generic tiku_mpu_lock_nvm() still drives tiku_mem_arch_nvm_flush().
 */
uint16_t tiku_mpu_arch_unlock_nvm(void) {
    uint16_t saved = stub_mpusam;
    stub_mpusam = (uint16_t)(saved | 0x0222U);
    mpu_set_nvm_ap(0U /* RW for the window */);
    return saved;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved_state) {
    tiku_mpu_arch_set_sam(saved_state);
    /* Nest-safe: restore the AP the SAVED state implies (an inner
     * lock inside a still-open outer window keeps the region RW). */
    mpu_set_nvm_ap(((saved_state & 0x0222U) == 0U) ? 1U : 0U);
}

/**
 * @brief Read back whether MPU region 3 (.uninit) is currently RO.
 *
 * Decodes the live RASR.AP field so the write-protection flip is
 * positively assertable on hardware (PMSAv7 twin of the apollo510
 * getter).
 */
uint8_t tiku_mpu_arch_nvm_region_ro(void) {
    uint32_t rasr, ap;
    MPU->RNR = MPU_REGION_NVM;
    __DSB();
    rasr = MPU->RASR;
    /* A disabled region protects nothing -- report not-RO, so a wiped region 3
     * (RASR=0 => AP=0, which is NOT AP_FULL) cannot masquerade as read-only.
     * This is what lets the "RO immediately after init" test catch a clear that
     * disables the .uninit envelope. */
    if ((rasr & MPU_RASR_ENABLE_Msk) == 0U) {
        return 0u;
    }
    ap = (rasr >> MPU_RASR_AP_Pos) & 0x7U;
    return (ap == ARM_MPU_AP_FULL) ? 0u : 1u;
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
/* Diagnostics + test hooks (device-agnostic)                                */
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
/* Fault handlers -- strong overrides of the weak crt_early aliases.         */
/* The ARMv7-M SCB fault registers (CFSR/MMFAR/HFSR/SHCSR) match the M55.     */
/*---------------------------------------------------------------------------*/


/* Fault-time console dump (twin of the apollo510 one): the warm-
 * durable record has not been proven to survive the post-fault reset,
 * so the UART line emitted right here is the reliable diagnostic. */
extern void tiku_uart_putc(char c);

static void fault_puthex(uint32_t v) {
    static const char hx[] = "0123456789abcdef";
    int i;
    for (i = 28; i >= 0; i -= 4) {
        tiku_uart_putc(hx[(v >> i) & 0xFU]);
    }
}

static void fault_dump(const char *tag, uint32_t cfsr, uint32_t addr) {
    const char *p;
    for (p = "\r\n[TM:FAULT] "; *p; p++) { tiku_uart_putc(*p); }
    for (p = tag; *p; p++)                { tiku_uart_putc(*p); }
    for (p = " cfsr=0x"; *p; p++)         { tiku_uart_putc(*p); }
    fault_puthex(cfsr);
    for (p = " addr=0x"; *p; p++)         { tiku_uart_putc(*p); }
    fault_puthex(addr);
    tiku_uart_putc('\r');
    tiku_uart_putc('\n');
}

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

    fault_dump("memmanage", cfsr, mpu_diag.last_fault_addr);

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

    fault_dump("hardfault", cfsr, mpu_diag.last_fault_addr);

    __DSB();
    NVIC_SystemReset();
    for (;;) { }
}
