/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - RP2350 MPU driver (real ARMv8-M MPU enforcement)
 *
 * The kernel MPU layer was designed against the MSP430 segment-based
 * SAM/CTL register model: 3 segments (SEG1/2/3) × 3 permission bits
 * (R/W/X) packed into a 16-bit SAM word. SEG3 is "the NVM region" —
 * the kernel's persist + lc-persist + init layers wrap NVM writes
 * with tiku_mpu_unlock_nvm() / lock_nvm() so the MPU enforces "writes
 * to NVM only happen during the explicit unlock window".
 *
 * On RP2350 there's no FRAM, but the same protection model is useful:
 * .uninit (the SRAM region that survives warm reset and stands in for
 * NVM in this port) gets the same R+X-by-default / R+W+X-while-
 * unlocked treatment, enforced by the Cortex-M33's ARMv8-M MPU.
 *
 * Implementation:
 *   - MPU region 0 covers .uninit. Default state is read-only;
 *     unlock_nvm flips it to read-write; lock_nvm restores.
 *   - MPU_CTRL.PRIVDEFENA is set so unmapped memory uses the default
 *     privileged R/W policy — every other memory access keeps working
 *     without needing per-region setup.
 *   - SEG1 / SEG2 are bookkept in software (the SAM word) so the
 *     kernel-level API tests still see the expected register-model
 *     values, but they're not enforced in hardware (no kernel code
 *     today asks for hardware enforcement of those segments — they
 *     correspond to .text and .data on MSP430, which on RP2350 are
 *     already correctly accessible via the default policy).
 *   - MemManage exception (vector 4) is wired to bump a violation
 *     counter, latch the faulting address into MMFAR for diagnosis,
 *     and trigger a system reset. A buggy write to .uninit without
 *     unlocking now actually faults — this is the "MPU is real"
 *     property that distinguishes this from the previous bookkeeping
 *     stub.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "tiku_rp2350_regs.h"
#include "tiku_cpu_freq_boot_arch.h"
#include <hal/tiku_cpu.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Linker symbols for the protected region                                   */
/*---------------------------------------------------------------------------*/

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;

/*---------------------------------------------------------------------------*/
/* Software-bookkept MSP430-style register file                              */
/*                                                                            */
/* Kept across the API surface so existing tests (which read SAM back and    */
/* compare bit patterns) see the values they expect. The hardware MPU is    */
/* programmed in lock-step for the segment we actually enforce (SEG3 = NVM). */
/*---------------------------------------------------------------------------*/

static uint16_t stub_mpuctl0;
static uint16_t stub_mpuctl1;     /* violation flags */
static uint16_t stub_mpusam = TIKU_MPU_DEFAULT_SAM;
static uint16_t stub_mpusegb1;
static uint16_t stub_mpusegb2;

/*---------------------------------------------------------------------------*/
/* Persistent diagnostic state                                               */
/*                                                                            */
/* Lives in the .mpu_diag NOLOAD section (defined in the linker script,      */
/* placed BEFORE .uninit so it sits outside the MPU-protected range). This  */
/* lets the MemManage handler bump the violation counter even though the    */
/* fault that brought us here means we can't safely write to .uninit, and   */
/* lets the test fleet see "previous boot crashed on MPU violation" by      */
/* reading state that survived the post-fault reset.                         */
/*                                                                            */
/* On cold boot the .mpu_diag bytes are random; the magic word disambiguates */
/* "we have valid state" from "fresh power-up", and tiku_mpu_arch_init_      */
/* segments() zeroes the struct + writes magic on first boot.                */
/*---------------------------------------------------------------------------*/

#define TIKU_MPU_DIAG_MAGIC  0x4D505550U   /* 'M''P''U''P' */

struct tiku_mpu_diag {
    uint32_t magic;
    uint32_t violation_count;     /* total faults across all warm boots */
    uint32_t last_fault_addr;     /* MMFAR snapshot from most recent fault */
    uint32_t last_fault_mmfsr;    /* MMFSR cause bits from most recent fault */
    uint32_t expect_fault;        /* test scaffold: 1 = test armed for fault */
    uint32_t last_fault_cfsr;     /* full CFSR — MMFSR/BFSR/UFSR */
    uint32_t last_fault_hfsr;     /* HFSR — bit 30 (FORCED) means escalated */
    uint32_t last_fault_ipsr;     /* exception number that handled the fault */
};

__attribute__((section(".mpu_diag")))
static volatile struct tiku_mpu_diag mpu_diag;

/*---------------------------------------------------------------------------*/
/* Hardware MPU helpers                                                      */
/*---------------------------------------------------------------------------*/

#define MPU_REGION_NVM  0U   /* hardware region we use for SEG3 */

static inline void mpu_dsb_isb(void) {
    __asm__ volatile ("dsb 0xF" ::: "memory");
    __asm__ volatile ("isb 0xF" ::: "memory");
}

/* Compute the RBAR value for the .uninit region with the given AP bits.
 * Aligns the base down to the 32-byte MPU granule. */
static uint32_t mpu_rbar_uninit(uint32_t ap_bits) {
    uint32_t base = (uint32_t)&__uninit_start & ~0x1FU;
    return base
         | (0U << 3)               /* SH = Non-shareable */
         | ap_bits                  /* RW or RO */
         | RP2350_MPU_RBAR_XN;      /* never execute from NVM */
}

static uint32_t mpu_rlar_uninit(void) {
    /* RLAR layout per ARMv8-M ARM B11.2.10:
     *   bits[31:5]  LIMIT       - high bits of the inclusive limit
     *                             address (low 5 bits implicitly 0x1F)
     *   bits[ 4:1]  AttrIndx    - index into MAIR0/MAIR1
     *   bit[0]      EN          - region enable
     *
     * Compute LIMIT bits[31:5] by clearing the low 5 bits of the
     * inclusive end address. Earlier bug: `(end - 1) | 0x1F` set the
     * low 5 bits to 0x1F, which the hardware interpreted as
     * AttrIndx=15 (pointing at MAIR1[31:24]) instead of AttrIndx=0
     * (MAIR0[7:0] = Normal Non-cacheable). Default MAIR1 is 0 =
     * Device-nGnRnE, which has very strict semantics for SRAM and
     * subtly hangs accesses that should "just work". */
    uint32_t end          = (uint32_t)&__uninit_end;
    uint32_t limit_field  = (end - 1U) & ~0x1FU;
    return limit_field
         | (0U << 1)               /* AttrIndx = 0 -> MAIR0[7:0] */
         | RP2350_MPU_RLAR_EN;
}

/* Reprogram region 0 with the given AP bits. Caller is responsible
 * for any necessary IRQ masking — these reads/writes are not atomic
 * w.r.t. an interrupt that also touches the MPU. */
static void mpu_set_nvm_ap(uint32_t ap_bits) {
    _RP2350_REG(RP2350_MPU_RNR)  = MPU_REGION_NVM;
    _RP2350_REG(RP2350_MPU_RBAR) = mpu_rbar_uninit(ap_bits);
    _RP2350_REG(RP2350_MPU_RLAR) = mpu_rlar_uninit();
    mpu_dsb_isb();
}

/* Read back the AP bits currently programmed for region 0. */
static uint32_t mpu_get_nvm_ap(void) {
    _RP2350_REG(RP2350_MPU_RNR) = MPU_REGION_NVM;
    return _RP2350_REG(RP2350_MPU_RBAR) & RP2350_MPU_RBAR_AP_MASK;
}

/*---------------------------------------------------------------------------*/
/* HAL implementation                                                        */
/*---------------------------------------------------------------------------*/

uint16_t tiku_mpu_arch_get_sam(void)   { return stub_mpusam; }
void     tiku_mpu_arch_set_sam(uint16_t sam) {
    stub_mpuctl0 = 0xA500U;             /* mirror MSP430 password write */
    stub_mpusam  = sam;
    stub_mpuctl0 = 0xA500U | 0x0001U;   /* password | enable */

    /* Translate SEG3's W bit (bit 9 of SAM) into the MPU AP setting
     * for region 0. SEG1/SEG2 are bookkept only — see file header. */
    uint32_t ap = (sam & 0x0200U) ? RP2350_MPU_RBAR_AP_RW_ANY
                                  : RP2350_MPU_RBAR_AP_RO_ANY;
    mpu_set_nvm_ap(ap);
}

uint16_t tiku_mpu_arch_get_ctl(void)   { return stub_mpuctl0; }

void tiku_mpu_arch_disable_irq(void) { tiku_cpu_irq_disable(); }
void tiku_mpu_arch_enable_irq(void)  { tiku_cpu_irq_enable(); }

void tiku_mpu_arch_init_segments(void) {
    /* Cold-boot detection: if the magic word is missing the .mpu_diag
     * region is whatever random bytes were in SRAM at power-up. Zero
     * the struct and write magic. On warm reset (post-fault) the magic
     * is preserved and we keep the existing counters — that's how the
     * violation-detect test sees "yes the previous boot faulted". */
    if (mpu_diag.magic != TIKU_MPU_DIAG_MAGIC) {
        mpu_diag.magic            = TIKU_MPU_DIAG_MAGIC;
        mpu_diag.violation_count  = 0U;
        mpu_diag.last_fault_addr  = 0U;
        mpu_diag.last_fault_mmfsr = 0U;
        mpu_diag.expect_fault     = 0U;
    }

    /* Mirror the MSP430 host stub layout so any code that reads SEGB
     * registers sees a familiar shape (boundaries at upper third /
     * two-thirds of the MSP430 64 KB MPU window). */
    stub_mpusegb1 = 0x0800U;   /* 0x8000 >> 4 */
    stub_mpusegb2 = 0x0C00U;   /* 0xC000 >> 4 */

    /* Configure MAIR0 byte 0 = Normal Non-cacheable. SRAM doesn't
     * need cache attributes, and Non-cacheable keeps the access
     * semantics simple under the MPU. */
    _RP2350_REG(RP2350_MPU_MAIR0) = (uint32_t)RP2350_MPU_MAIR_NORMAL_NC;

    /* Disable MPU before reconfiguring. */
    _RP2350_REG(RP2350_MPU_CTRL) = 0U;
    mpu_dsb_isb();

    /* Region 0 = .uninit, default RO (locked). */
    mpu_set_nvm_ap(RP2350_MPU_RBAR_AP_RO_ANY);

    /* Enable MPU + PRIVDEFENA so unmapped memory keeps using default
     * privileged access. HFNMIENA stays off — we don't want the MPU
     * to apply during HardFault / NMI (would risk a fault loop in
     * the panic path). */
    _RP2350_REG(RP2350_MPU_CTRL) = RP2350_MPU_CTRL_ENABLE
                                 | RP2350_MPU_CTRL_PRIVDEFENA;
    mpu_dsb_isb();

    /* Set MemManage exception priority to 0 (highest configurable)
     * and enable it. Without this, an MPU fault from Thread mode
     * was being FORCED to HardFault — symptom: HFSR.FORCED set with
     * CFSR.DACCVIOL = 1 and IPSR = 3 (HardFault) instead of 4
     * (MemManage). The Pico boot ROM apparently leaves SHPR1 byte 0
     * (MemManage priority) in a state that prevents MemManage from
     * preempting from Thread mode despite IPSR=0; setting it to 0
     * explicitly fixes that. SHPR1 byte 0 = 0xE000ED18. */
    *(volatile uint8_t *)0xE000ED18U = 0U;

    /* Enable MemManage exception unconditionally at init time (rather
     * than relying on tiku_mpu_arch_enable_violation_nmi being
     * called separately) so any MPU fault from any kernel path
     * vectors to MemManage instead of escalating. */
    _RP2350_REG(RP2350_SCB_SHCSR) |= RP2350_SCB_SHCSR_MEMFAULTENA;
    mpu_dsb_isb();
}

/*---------------------------------------------------------------------------*/
/* Test-scaffold API                                                         */
/*                                                                            */
/* Used by test_mpu_violation_detect to perform a "trigger fault, expect    */
/* reset, verify counter incremented after reboot" handshake. These would   */
/* normally not be part of the production MPU surface — they live in the    */
/* arch file because mpu_diag is file-scope. */
/*---------------------------------------------------------------------------*/

uint32_t tiku_mpu_arch_violation_count(void) {
    return mpu_diag.violation_count;
}

uint32_t tiku_mpu_arch_last_fault_addr(void) {
    return mpu_diag.last_fault_addr;
}

uint32_t tiku_mpu_arch_last_fault_cfsr(void) {
    return mpu_diag.last_fault_cfsr;
}

uint32_t tiku_mpu_arch_last_fault_hfsr(void) {
    return mpu_diag.last_fault_hfsr;
}

uint32_t tiku_mpu_arch_last_fault_ipsr(void) {
    return mpu_diag.last_fault_ipsr;
}

uint32_t tiku_mpu_arch_test_expect_fault(void) {
    return mpu_diag.expect_fault;
}

void tiku_mpu_arch_test_arm_fault(void) {
    /* Arm the test scaffold: the upcoming MPU fault is expected;
     * the MemManage handler will set this flag back to a "fault
     * was observed" sentinel after the reset. */
    mpu_diag.expect_fault = 1U;
}

void tiku_mpu_arch_test_clear_violation(void) {
    mpu_diag.violation_count  = 0U;
    mpu_diag.last_fault_addr  = 0U;
    mpu_diag.last_fault_mmfsr = 0U;
    mpu_diag.expect_fault     = 0U;
}

void tiku_mpu_arch_set_default_protection(void) {
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm) {
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t mask  = (uint16_t)0x07U << shift;
    uint16_t sam   = stub_mpusam;

    sam = (uint16_t)((sam & ~mask) |
                     (((uint16_t)perm & 0x07U) << shift));
    tiku_mpu_arch_set_sam(sam);
}

uint16_t tiku_mpu_arch_unlock_nvm(void) {
    /* Snapshot current SAM so caller can restore exactly via lock_nvm.
     * Then OR in the W bits across all three segments (matches MSP430
     * driver behaviour) and flip the hardware NVM region to RW. */
    uint16_t saved = stub_mpusam;
    stub_mpusam = (uint16_t)(saved | 0x0222U);
    mpu_set_nvm_ap(RP2350_MPU_RBAR_AP_RW_ANY);
    return saved;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved_state) {
    /* Restore the SAM word the caller stashed, and program the
     * hardware NVM region to whatever AP that implies for SEG3. */
    tiku_mpu_arch_set_sam(saved_state);
}

uint16_t tiku_mpu_arch_get_violation_flags(void) {
    return stub_mpuctl1;
}

void tiku_mpu_arch_clear_violation_flags(void) {
    stub_mpuctl1 = 0U;
}

void tiku_mpu_arch_enable_violation_nmi(void) {
    /* On Cortex-M the closest equivalent to the MSP430 "violation
     * NMI" is enabling the MemManage exception so MPU faults vector
     * to MemManage_Handler instead of escalating to HardFault. */
    _RP2350_REG(RP2350_SCB_SHCSR) |= RP2350_SCB_SHCSR_MEMFAULTENA;
    mpu_dsb_isb();
    stub_mpuctl0 |= 0x0010U;            /* mirror MPU_SEGIE in bookkeeping */
}

/*---------------------------------------------------------------------------*/
/* MemManage handler — provides the strong definition that overrides         */
/* the weak alias in tiku_crt_early.c.                                       */
/*---------------------------------------------------------------------------*/

/* UART_DR for raw byte poke from the fault handler — avoids any
 * printf/stack machinery in case those would themselves fault while
 * we're trying to diagnose handler reachability. */
#define MPU_DIAG_UART_DR  ((volatile uint32_t *)0x40070000U)
static inline void mpu_diag_putc(char c) {
    *MPU_DIAG_UART_DR = (uint32_t)(uint8_t)c;
}

void tiku_rp2350_mem_fault_handler(void) {
    mpu_diag_putc('H');

    uint32_t cfsr = _RP2350_REG(RP2350_SCB_CFSR);
    uint32_t mmfsr = cfsr & 0xFFU;
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(ipsr));

    if (mmfsr & RP2350_SCB_MMFSR_MMARVALID) {
        mpu_diag.last_fault_addr = _RP2350_REG(RP2350_SCB_MMFAR);
    }
    mpu_diag.last_fault_mmfsr = mmfsr;
    mpu_diag.last_fault_cfsr  = cfsr;
    mpu_diag.last_fault_hfsr  = _RP2350_REG(RP2350_SCB_HFSR);
    mpu_diag.last_fault_ipsr  = ipsr;
    mpu_diag_putc('1');

    /* Bookkeeping: bump persistent counter (survives the reset we're
     * about to trigger), OR the MMFSR cause bits into the MSP430-style
     * violation flag word, and if the test scaffold armed an expected
     * fault, transition the flag to the "fault was observed" sentinel
     * so the post-reset boot can verify enforcement worked. */
    mpu_diag.violation_count++;
    stub_mpuctl1 |= (uint16_t)mmfsr;
    if (mpu_diag.expect_fault == 1U) {
        mpu_diag.expect_fault = 2U;   /* observed */
    }
    mpu_diag_putc('2');

    /* Clear the latched fault status (W1C). */
    _RP2350_REG(RP2350_SCB_CFSR) = cfsr;
    mpu_diag_putc('3');

    /* Recovery policy: a synchronous data-access violation cannot be
     * "stepped over" without decoding the faulting instruction's
     * length, and silently letting the offending store happen anyway
     * defeats the whole point of the MPU. Trigger a system reset; the
     * post-reset boot path sees the .mpu_diag counter incremented and
     * can log / annotate /sys/boot/mpu/violations or run the violation-
     * detect test's pass-on-second-boot path. */
    mpu_diag_putc('R');
    _RP2350_REG(RP2350_SCB_AIRCR) =
        RP2350_SCB_AIRCR_VECTKEY | RP2350_SCB_AIRCR_SYSRESET;

    /* Ensure the reset takes effect before any further code runs. */
    mpu_dsb_isb();
    mpu_diag_putc('S');   /* if seen, AIRCR write didn't actually reset */
    for (;;) { /* spin until reset asserts */ }
}

/* HardFault handler — strong override of the weak alias in
 * tiku_crt_early.c. Captures CFSR/HFSR/MMFAR into mpu_diag so the
 * test (or a debugger) can see why HardFault was taken instead of
 * the more specific configurable handler. */
void tiku_rp2350_hard_fault_handler(void) {
    /* Snapshot the fault status so boot 2 can read what happened.
     * HFSR.FORCED set => a configurable fault escalated to HardFault
     * because its handler couldn't be taken (disabled, masked, or
     * pre-empted). HFSR is at SCB_BASE + 0x2C. */
    uint32_t cfsr = _RP2350_REG(RP2350_SCB_CFSR);
    uint32_t hfsr = _RP2350_REG(RP2350_SCB_HFSR);
    uint32_t mmfar = _RP2350_REG(RP2350_SCB_MMFAR);
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(ipsr));

    mpu_diag.last_fault_mmfsr = (uint16_t)(cfsr & 0xFFU);
    mpu_diag.last_fault_addr  = mmfar;
    mpu_diag.last_fault_cfsr  = cfsr;
    mpu_diag.last_fault_hfsr  = hfsr;
    mpu_diag.last_fault_ipsr  = ipsr;
    /* Re-purpose the magic field to also smuggle CFSR/HFSR + IPSR
     * across the reset for boot-2 diagnostic. (We restore the magic
     * before returning so the next boot's init still sees valid
     * state — actually, simpler: just stuff into expect_fault sentinel
     * variants.) */
    if (mpu_diag.expect_fault == 1U) {
        mpu_diag.expect_fault = 3U;   /* HardFault path observed */
    }
    mpu_diag.violation_count++;

    /* Drain those values out via UART so boot-1 transcript shows
     * them even before reset. CFSR and HFSR are 32 bits → print as
     * hex. mpu_diag_putc is single-byte so do this byte-by-byte
     * to avoid pulling in tiku_uart_printf from a fault context. */
    const char hex[] = "0123456789abcdef";
    mpu_diag_putc('!'); mpu_diag_putc('H'); mpu_diag_putc('F');
    mpu_diag_putc(' '); mpu_diag_putc('I'); mpu_diag_putc('P');
    mpu_diag_putc('S'); mpu_diag_putc('R'); mpu_diag_putc('=');
    for (int i = 7; i >= 0; i--) {
        mpu_diag_putc(hex[(ipsr >> (i*4)) & 0xFU]);
    }
    mpu_diag_putc(' '); mpu_diag_putc('C'); mpu_diag_putc('F');
    mpu_diag_putc('S'); mpu_diag_putc('R'); mpu_diag_putc('=');
    for (int i = 7; i >= 0; i--) {
        mpu_diag_putc(hex[(cfsr >> (i*4)) & 0xFU]);
    }
    mpu_diag_putc(' '); mpu_diag_putc('H'); mpu_diag_putc('F');
    mpu_diag_putc('S'); mpu_diag_putc('R'); mpu_diag_putc('=');
    for (int i = 7; i >= 0; i--) {
        mpu_diag_putc(hex[(hfsr >> (i*4)) & 0xFU]);
    }
    mpu_diag_putc('\n');

    /* Try the same reset path as the MemManage handler. */
    _RP2350_REG(RP2350_SCB_AIRCR) =
        RP2350_SCB_AIRCR_VECTKEY | RP2350_SCB_AIRCR_SYSRESET;
    for (;;) { /* spin */ }
}
