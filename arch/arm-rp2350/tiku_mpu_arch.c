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
 *   - Six MPU regions for full W^X (Write-XOR-Execute) coverage
 *     plus a stack-overflow guard:
 *       Region 0  SEG3      .uninit                       — RO (locked)
 *                                                           / RW (unlocked), XN
 *       Region 1  SEG1      flash 0x10000000..end         — RX
 *       Region 2  SEG2a     SRAM 0x20000000..uninit_start — RW + XN
 *       Region 3  SEG2b     SRAM uninit_end..guard_base   — RW + XN
 *       Region 4  STACK_GD  32-byte guard just below the stack — RO + XN
 *       Region 5  SEG2c     SRAM above the guard..sram_end — RW + XN
 *     All six are non-overlapping (M33 overlap behaviour is
 *     implementation-defined and unsafe). The guard sits
 *     MPU_STACK_RESERVED_BYTES below __sram_end; a descending stack
 *     that walks past that budget faults on the next push instead of
 *     silently corrupting .data / .bss / .uninit.
 *   - MPU_CTRL.PRIVDEFENA is set so other memory (peripherals at
 *     0x40000000+, SCS at 0xE0000000+, XIP cache control, boot ROM)
 *     keeps using the default privileged R/W policy without consuming
 *     more regions.
 *   - The SAM bookkeeping word still tracks SEG1/SEG2/SEG3 R/W/X bits
 *     so the kernel-level API tests pass unchanged, but only SEG3's
 *     W bit is wired through to actually move hardware permissions.
 *     SEG1/SEG2 hardware permissions are FIXED at the safe defaults
 *     above — calling tiku_mpu_set_permissions(SEG1/SEG2, anything)
 *     updates the bookkeeping but doesn't make flash writable or
 *     SRAM executable. Doing either would brick the kernel: flash
 *     can't physically take stores via XIP, and a writable SRAM that
 *     was suddenly stripped of XN would let a stack-smash become a
 *     code-injection. The MSP430 SAM model didn't anticipate W^X;
 *     we keep its API surface but apply the protection that actually
 *     makes sense on this architecture.
 *   - MemManage exception (vector 4) is wired to bump a violation
 *     counter, latch the faulting address into MMFAR for diagnosis,
 *     and trigger a system reset. Three faulting paths are covered
 *     by the test suite: write to .uninit (DACCVIOL, MMFAR in NVM
 *     range), write to .text (DACCVIOL, MMFAR in flash range), and
 *     execute from SRAM (IACCVIOL, MMFAR not valid).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "tiku_rp2350_regs.h"
#include "tiku_cpu_freq_boot_arch.h"
#include <hal/tiku_cpu.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Linker symbols for the protected regions                                  */
/*                                                                            */
/* The MPU regions are pinned to the linker-section boundaries so the         */
/* protection always tracks the actual code/data layout — adding a new        */
/* .text symbol or growing .bss reshapes the regions automatically.           */
/*---------------------------------------------------------------------------*/

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;
extern uint32_t __sram_start;
extern uint32_t __sram_end;
extern uint32_t __flash_start;
extern uint32_t __flash_end;

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
    /* Bitmask: which W^X-survival sub-tests have already passed across
     * the current test-suite run. Bit 0 = SEG3 write, bit 1 = SEG1
     * write, bit 2 = SEG2 exec. Lets the SEG1/SEG2 tests sequence
     * across multiple chip resets — once a sub-test verifies, it
     * sets its bit so subsequent boots skip past it and arm the
     * next-in-sequence sub-test. Cleared explicitly by the test
     * scaffold when the user wants to re-run from scratch. */
    uint32_t test_done_mask;
};

#define TIKU_MPU_TEST_DONE_SEG3   (1U << 0)
#define TIKU_MPU_TEST_DONE_SEG1   (1U << 1)
#define TIKU_MPU_TEST_DONE_SEG2   (1U << 2)
#define TIKU_MPU_TEST_DONE_SG     (1U << 3)   /* stack-guard sub-test */

__attribute__((section(".mpu_diag")))
static volatile struct tiku_mpu_diag mpu_diag;

/*---------------------------------------------------------------------------*/
/* Hardware MPU helpers                                                      */
/*---------------------------------------------------------------------------*/

/* Region assignment — one MPU region per protection class.  NOT
 * overlapping (ARMv8-M overlap behaviour is implementation-defined
 * on M33 and the spec strongly recommends against it). The SRAM
 * region post-.uninit splits into three pieces so a small RO+XN
 * "guard" sits at the bottom of the descending stack. */
#define MPU_REGION_NVM         0U   /* SEG3 = .uninit (RO/RW + XN) */
#define MPU_REGION_TEXT        1U   /* SEG1 = flash (.text + .rodata, RX) */
#define MPU_REGION_SRAM_LO     2U   /* SEG2a = SRAM below uninit (RW + XN) */
#define MPU_REGION_SRAM_MID    3U   /* SEG2b = SRAM above uninit, below
                                       stack guard (RW + XN) */
#define MPU_REGION_STACK_GUARD 4U   /* SG: 32-byte RO+XN guard at the
                                       bottom of the descending stack;
                                       a stack-frame STR that walks past
                                       this address faults via MemManage
                                       instead of silently corrupting
                                       .bss / .data / .uninit */
#define MPU_REGION_SRAM_TOP    5U   /* SEG2c = SRAM above the guard, up
                                       to __sram_end (RW + XN); this is
                                       the live stack region */

/* Stack budget. The guard sits 32 bytes BELOW this offset from the top
 * of SRAM. Cortex-M33 in TikuOS configurations runs every process on
 * the main stack (no PSP); 8 KB is generous. Enlarge if a real workload
 * needs more headroom. */
#define MPU_STACK_RESERVED_BYTES   8192U
#define MPU_STACK_GUARD_BYTES      32U

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

/* Generic region programmer for the static SEG1/SEG2 regions. The
 * caller passes already-aligned base and inclusive end addresses; we
 * apply the standard AttrIndx=0 (Normal Non-cacheable) MAIR slot.
 * SH=Non-shareable is the right choice for the single-core view that
 * the kernel and its drivers operate against. */
static void mpu_program_region(uint32_t region, uint32_t base,
                               uint32_t end_inclusive,
                               uint32_t ap_bits, uint32_t xn_bit) {
    uint32_t base_field  = base & ~0x1FU;
    uint32_t limit_field = end_inclusive & ~0x1FU;

    _RP2350_REG(RP2350_MPU_RNR)  = region;
    _RP2350_REG(RP2350_MPU_RBAR) = base_field
                                 | (0U << 3)         /* SH = NS */
                                 | ap_bits
                                 | xn_bit;
    _RP2350_REG(RP2350_MPU_RLAR) = limit_field
                                 | (0U << 1)         /* AttrIndx 0 */
                                 | RP2350_MPU_RLAR_EN;
    mpu_dsb_isb();
}

/* Region 1: SEG1 = flash (.text + .rodata + .vectors). RX only.
 * Stores into flash via normal CPU writes are physically rejected by
 * XIP anyway, but having the MPU also reject them surfaces the bad
 * pointer immediately as a MemManage instead of a silent no-op. */
static void mpu_program_seg1_text(void) {
    uint32_t base = (uint32_t)&__flash_start;
    uint32_t end  = (uint32_t)&__flash_end - 1U;
    mpu_program_region(MPU_REGION_TEXT, base, end,
                       RP2350_MPU_RBAR_AP_RO_ANY, 0U /* exec OK */);
}

/* Region 2: low half of SEG2 — SRAM from base up to (but not
 * including) .uninit. Covers .data, .bss, .mpu_diag, and any free
 * SRAM that a compiler-emitted constructor table might land in.
 * RW + XN.
 *
 * .mpu_diag sits inside this region by design — the MemManage
 * handler still gets RW access (region is RW), and code cannot
 * execute from .mpu_diag bytes (XN bit). The alternative of
 * leaving .mpu_diag uncovered would let it inherit PRIVDEFENA's
 * RW+EXEC default; not actively dangerous (no kernel path branches
 * there) but loses defence-in-depth. */
static void mpu_program_seg2_sram_lo(void) {
    uint32_t base = (uint32_t)&__sram_start;
    uint32_t end  = (uint32_t)&__uninit_start - 1U;
    mpu_program_region(MPU_REGION_SRAM_LO, base, end,
                       RP2350_MPU_RBAR_AP_RW_ANY,
                       RP2350_MPU_RBAR_XN);
}

/* Compute the stack guard's base address. The guard is 32 bytes wide
 * sitting MPU_STACK_RESERVED_BYTES below the top of SRAM. This address
 * is the boundary between the SRAM_MID region (RW+XN; covers anything
 * the kernel might place between .uninit and the live stack region)
 * and the SRAM_TOP region (RW+XN; the live stack). When the descending
 * stack pointer dips below MPU_STACK_RESERVED_BYTES of usage, the next
 * STR/PUSH lands inside the guard and the MPU faults. */
static inline uint32_t mpu_stack_guard_base(void) {
    return (uint32_t)&__sram_end -
           MPU_STACK_RESERVED_BYTES -
           MPU_STACK_GUARD_BYTES;
}

/* Region 3 (SRAM mid): from end of .uninit up to the bottom of the
 * stack guard. RW + XN. This is where the kernel's static data
 * doesn't reach and where the heap WOULD live if TikuOS used a
 * heap (it doesn't -- static allocation only -- so this region is
 * usually unused at runtime). */
static void mpu_program_seg2_sram_mid(void) {
    uint32_t base = (uint32_t)&__uninit_end;
    uint32_t end  = mpu_stack_guard_base() - 1U;
    mpu_program_region(MPU_REGION_SRAM_MID, base, end,
                       RP2350_MPU_RBAR_AP_RW_ANY,
                       RP2350_MPU_RBAR_XN);
}

/* Region 4 (stack guard): a 32-byte RO+XN slot at exactly
 * `mpu_stack_guard_base()`. The kernel's stack starts at __sram_end
 * and grows down; once it has consumed MPU_STACK_RESERVED_BYTES bytes
 * the next push lands here and faults. RO so writes fault, XN so a
 * write-then-jump can't smuggle code in either. */
static void mpu_program_stack_guard(void) {
    uint32_t base = mpu_stack_guard_base();
    uint32_t end  = base + MPU_STACK_GUARD_BYTES - 1U;
    mpu_program_region(MPU_REGION_STACK_GUARD, base, end,
                       RP2350_MPU_RBAR_AP_RO_ANY,
                       RP2350_MPU_RBAR_XN);
}

/* Region 5 (SRAM top): the live stack region from just above the
 * guard up to __sram_end. RW + XN. Stack pushes succeed here; the
 * XN bit prevents stack-buffer-overflow shellcode from being
 * executed. */
static void mpu_program_seg2_sram_top(void) {
    uint32_t base = mpu_stack_guard_base() + MPU_STACK_GUARD_BYTES;
    uint32_t end  = (uint32_t)&__sram_end - 1U;
    mpu_program_region(MPU_REGION_SRAM_TOP, base, end,
                       RP2350_MPU_RBAR_AP_RW_ANY,
                       RP2350_MPU_RBAR_XN);
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
        mpu_diag.test_done_mask   = 0U;
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

    /* Disable MPU before reconfiguring. ARMv8-M lets us program
     * regions while the MPU is enabled if we DSB/ISB after, but
     * disabling first avoids any window where a partially-programmed
     * region could trigger a fault on an in-flight memory access. */
    _RP2350_REG(RP2350_MPU_CTRL) = 0U;
    mpu_dsb_isb();

    /* Region 0 = .uninit, default RO (locked). */
    mpu_set_nvm_ap(RP2350_MPU_RBAR_AP_RO_ANY);

    /* Region 1 = flash (.text + .rodata): RX. */
    mpu_program_seg1_text();

    /* Regions 2/3/4/5 = SRAM split into four pieces:
     *   2: low SRAM up to .uninit (data + bss + mpu_diag, RW + XN)
     *   3: mid SRAM after .uninit, up to the stack guard (RW + XN)
     *   4: 32-byte stack guard at the bottom of the descending
     *      stack (RO + XN) -- a stack push that walks past
     *      MPU_STACK_RESERVED_BYTES of usage faults here
     *   5: live stack region above the guard (RW + XN)
     * The non-overlapping split means we don't depend on the
     * implementation-defined ARMv8-M behaviour for overlapping
     * regions. SRAM-resident shellcode (write+jump) still faults on
     * the jump anywhere in regions 2/3/5 because all carry XN. */
    mpu_program_seg2_sram_lo();
    mpu_program_seg2_sram_mid();
    mpu_program_stack_guard();
    mpu_program_seg2_sram_top();

    /* Enable MPU + PRIVDEFENA so unmapped memory (peripherals at
     * 0x40000000+, SCS/MPU at 0xE0000000+, XIP cache control,
     * boot ROM) keeps using default privileged access without
     * burning more regions. HFNMIENA stays off — we don't want the
     * MPU to apply during HardFault/NMI (would risk a fault loop
     * in the panic path). */
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

uint32_t tiku_mpu_arch_test_done_mask(void) {
    return mpu_diag.test_done_mask;
}

void tiku_mpu_arch_test_mark_done(uint32_t bit) {
    mpu_diag.test_done_mask |= bit;
}

void tiku_mpu_arch_test_clear_done_mask(void) {
    mpu_diag.test_done_mask = 0U;
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

void tiku_rp2350_mem_fault_handler(void) {
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

    /* Clear the latched fault status (W1C). */
    _RP2350_REG(RP2350_SCB_CFSR) = cfsr;

    /* Recovery policy: a synchronous data-access violation cannot be
     * "stepped over" without decoding the faulting instruction's
     * length, and silently letting the offending store happen anyway
     * defeats the whole point of the MPU. Trigger a system reset; the
     * post-reset boot path sees the .mpu_diag counter incremented and
     * can log / annotate /sys/boot/mpu/violations or run the violation-
     * detect test's pass-on-second-boot path. */
    _RP2350_REG(RP2350_SCB_AIRCR) =
        RP2350_SCB_AIRCR_VECTKEY | RP2350_SCB_AIRCR_SYSRESET;

    /* Ensure the reset takes effect before any further code runs. */
    mpu_dsb_isb();
    for (;;) { /* spin until reset asserts */ }
}

/* HardFault handler — strong override of the weak alias in
 * tiku_crt_early.c. Captures CFSR/HFSR/MMFAR into mpu_diag so a
 * debugger or the test fleet can see why HardFault was taken
 * instead of the more specific configurable handler. The test
 * scaffold uses expect_fault=3 (vs MemManage's expect_fault=2) to
 * tell the two paths apart on the post-reset boot. */
void tiku_rp2350_hard_fault_handler(void) {
    uint32_t cfsr  = _RP2350_REG(RP2350_SCB_CFSR);
    uint32_t hfsr  = _RP2350_REG(RP2350_SCB_HFSR);
    uint32_t mmfar = _RP2350_REG(RP2350_SCB_MMFAR);
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(ipsr));

    mpu_diag.last_fault_mmfsr = (uint16_t)(cfsr & 0xFFU);
    mpu_diag.last_fault_addr  = mmfar;
    mpu_diag.last_fault_cfsr  = cfsr;
    mpu_diag.last_fault_hfsr  = hfsr;
    mpu_diag.last_fault_ipsr  = ipsr;
    if (mpu_diag.expect_fault == 1U) {
        mpu_diag.expect_fault = 3U;   /* HardFault path observed */
    }
    mpu_diag.violation_count++;

    _RP2350_REG(RP2350_SCB_AIRCR) =
        RP2350_SCB_AIRCR_VECTKEY | RP2350_SCB_AIRCR_SYSRESET;
    for (;;) { /* spin until reset asserts */ }
}
