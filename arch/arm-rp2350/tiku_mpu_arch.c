/*
 * Tiku Operating System v0.06
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

/** @brief Software mirror of the MSP430-model MPU register file.
 *
 *  These variables shadow the four MSP430 MPU hardware registers so the
 *  kernel API (get_sam / set_sam / get_ctl / get_violation_flags) behaves
 *  identically to the real MSP430 driver. The hardware ARMv8-M MPU is
 *  programmed in lock-step only for the region that the RP2350 port
 *  actually enforces (SEG3 / .uninit); SEG1 and SEG2 bookkeeping is
 *  purely in software.
 */
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

/** @brief Magic sentinel for cold-boot detection in .mpu_diag ('MPUP'). */
#define TIKU_MPU_DIAG_MAGIC  0x4D505550U   /* 'M''P''U''P' */

/** @brief Persistent MPU diagnostic state that survives warm reset.
 *
 *  Placed in the .mpu_diag NOLOAD section, which sits outside the
 *  MPU-protected .uninit range. This lets the MemManage handler write
 *  diagnostics even though it is executing in an MPU-fault context and
 *  cannot safely touch .uninit. On cold boot the magic word is used to
 *  detect uninitialized SRAM; tiku_mpu_arch_init_segments() zeroes the
 *  struct and stamps magic on first power-up.
 */
struct tiku_mpu_diag {
    uint32_t magic;             /**< Sentinel — TIKU_MPU_DIAG_MAGIC if valid. */
    uint32_t violation_count;   /**< Total MPU faults across all warm boots. */
    uint32_t last_fault_addr;   /**< MMFAR snapshot from the most recent fault. */
    uint32_t last_fault_mmfsr;  /**< MMFSR cause bits from the most recent
                                     fault (byte 0 of CFSR). */
    uint32_t expect_fault;      /**< Test scaffold: 1 = fault armed, 2 = fault
                                     observed via MemManage, 3 = via HardFault. */
    uint32_t last_fault_cfsr;   /**< Full CFSR (MMFSR/BFSR/UFSR) snapshot. */
    uint32_t last_fault_hfsr;   /**< HFSR snapshot; bit 30 (FORCED) means the
                                     fault was escalated from a lower handler. */
    uint32_t last_fault_ipsr;   /**< Exception number active when the fault
                                     was handled (4 = MemManage, 3 = HardFault). */
    /** Bitmask: which W^X-survival sub-tests have already passed across
     * the current test-suite run. Bit 0 = SEG3 write, bit 1 = SEG1
     * write, bit 2 = SEG2 exec. Lets the SEG1/SEG2 tests sequence
     * across multiple chip resets — once a sub-test verifies, it
     * sets its bit so subsequent boots skip past it and arm the
     * next-in-sequence sub-test. Cleared explicitly by the test
     * scaffold when the user wants to re-run from scratch. */
    uint32_t test_done_mask;
    /** HFNMI-distinguish test phase counter.
     *  0 = idle / fresh; 1 = armed (waiting for fault);
     *  2 = "bogus write attempted"; 3 = "write completed, AIRCR pending". */
    uint32_t hfnmi_phase;
    /** Test scaffold flag: when 1, the HardFault handler writes to a
     *  flash address before issuing AIRCR to distinguish HFNMIENA=0
     *  (silent no-op) from HFNMIENA=1 (lockup). Always 0 in production. */
    uint32_t handler_misbehave;
};

/** @brief Bit assignments for mpu_diag.test_done_mask.
 *
 *  Each bit records that a W^X sub-test has already been verified on a
 *  previous boot, so the test runner can skip forward to the next
 *  sub-test without re-inducing a fault.
 */
#define TIKU_MPU_TEST_DONE_SEG3   (1U << 0)
#define TIKU_MPU_TEST_DONE_SEG1   (1U << 1)
#define TIKU_MPU_TEST_DONE_SEG2   (1U << 2)
#define TIKU_MPU_TEST_DONE_SG     (1U << 3)   /* stack-guard sub-test */

/** @brief The single mpu_diag instance placed in the .mpu_diag NOLOAD section.
 *
 *  Sits outside the ARMv8-M MPU-protected .uninit range so the MemManage
 *  and HardFault handlers can write to it unconditionally.  volatile because
 *  handlers running at exception priority modify it without going through
 *  normal call paths.
 */
__attribute__((section(".mpu_diag")))
static volatile struct tiku_mpu_diag mpu_diag;

/*---------------------------------------------------------------------------*/
/* Hardware MPU helpers                                                      */
/*---------------------------------------------------------------------------*/

/** @brief ARMv8-M MPU region index assignments.
 *
 *  Regions are non-overlapping; ARMv8-M overlap behaviour is
 *  implementation-defined on Cortex-M33 and the spec discourages it.
 *  The SRAM range above .uninit is split into three pieces so a small
 *  RO+XN "guard" can sit at the bottom of the descending stack.
 */
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

/** @brief Stack-guard sizing constants.
 *
 *  The guard is MPU_STACK_GUARD_BYTES wide and sits MPU_STACK_RESERVED_BYTES
 *  below the top of SRAM.  TikuOS runs all processes on the single main
 *  stack (no PSP); 8 KB headroom is generous for the current workloads.
 *  Enlarge MPU_STACK_RESERVED_BYTES if a profiling run shows the guard is
 *  being approached.
 */
/* Stack budget + guard sizing.  8 KB + a 32-byte guard proved wrong on
 * two counts (found by Tiku BASIC's string parser, July 2026): the
 * parser's KB-sized frames (1 KB string buffers, two recursion levels)
 * legitimately reach ~10 KB of stack, and a 32-byte guard is LEAPT by
 * KB-sized frame allocations -- SP lands below the guard without ever
 * touching it, execution continues in SRAM_MID, and only a stray local
 * that happens to fall inside the 32-byte window faults (BASIC's
 * PRINT LEFT$(A$,3) stored is_str at 0x2007fff8 -> MemManage -> reset
 * loop).  32 KB covers the deepest realistic BASIC nesting (~24 KB)
 * with margin; a 4 KB guard cannot be jumped by any frame smaller than
 * 4 KB, which bounds every frame in the tree today.  Update the copies
 * in TikuBench tests/memory/test_mem_mpu.c in lockstep. */
#define MPU_STACK_RESERVED_BYTES   32768U
#define MPU_STACK_GUARD_BYTES      4096U

/**
 * @brief Issue a full DSB + ISB memory barrier pair.
 *
 *  Required after every MPU register write to guarantee the new
 *  permissions are visible before the next instruction fetch or data
 *  access.
 */
static inline void mpu_dsb_isb(void) {
    __asm__ volatile ("dsb 0xF" ::: "memory");
    __asm__ volatile ("isb 0xF" ::: "memory");
}

/**
 * @brief Compute the RBAR value for the .uninit region.
 *
 *  Base is aligned down to the 32-byte ARMv8-M MPU granule.  SH is
 *  forced to Non-shareable and XN is always set — .uninit is never
 *  executable regardless of the AP setting.
 *
 * @param ap_bits  AP field bits (e.g. RP2350_MPU_RBAR_AP_RW_ANY or
 *                 RP2350_MPU_RBAR_AP_RO_ANY).
 * @return         Value ready to write to MPU_RBAR.
 */
static uint32_t mpu_rbar_uninit(uint32_t ap_bits) {
    uint32_t base = (uint32_t)&__uninit_start & ~0x1FU;
    return base
         | (0U << 3)               /* SH = Non-shareable */
         | ap_bits                  /* RW or RO */
         | RP2350_MPU_RBAR_XN;      /* never execute from NVM */
}

/**
 * @brief Compute the RLAR value for the .uninit region.
 *
 *  Sets AttrIndx=0 (MAIR0[7:0] = Normal Non-cacheable) and enables the
 *  region.  The LIMIT field is derived from __uninit_end with the low 5
 *  bits cleared per the ARMv8-M ARM B11.2.10 encoding.
 *
 * @return Value ready to write to MPU_RLAR.
 */
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

/**
 * @brief Reprogram MPU region 0 (.uninit / SEG3) with new AP bits.
 *
 *  Caller is responsible for any necessary IRQ masking — the sequence of
 *  RNR / RBAR / RLAR writes is not atomic with respect to an interrupt
 *  that also touches the MPU.
 *
 * @param ap_bits  AP field to apply (RP2350_MPU_RBAR_AP_RW_ANY or
 *                 RP2350_MPU_RBAR_AP_RO_ANY).
 */
static void mpu_set_nvm_ap(uint32_t ap_bits) {
    _RP2350_REG(RP2350_MPU_RNR)  = MPU_REGION_NVM;
    _RP2350_REG(RP2350_MPU_RBAR) = mpu_rbar_uninit(ap_bits);
    _RP2350_REG(RP2350_MPU_RLAR) = mpu_rlar_uninit();
    mpu_dsb_isb();
}

/**
 * @brief Program one ARMv8-M MPU region with the given protection attributes.
 *
 *  Applies AttrIndx=0 (Normal Non-cacheable, as configured in MAIR0 byte 0)
 *  and SH=Non-shareable.  The caller passes addresses that are already
 *  aligned to the 32-byte MPU granule; low 5 bits are cleared defensively.
 *
 * @param region        Region index (0–7) to program.
 * @param base          Inclusive base address of the region.
 * @param end_inclusive Inclusive end address (last byte covered).
 * @param ap_bits       AP field for the RBAR (read/write access policy).
 * @param xn_bit        RP2350_MPU_RBAR_XN to block execution, or 0.
 */
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

/**
 * @brief Program MPU region 1 — SEG1 (flash .text + .rodata + .vectors).
 *
 *  Flash is mapped read-execute only.  XIP hardware already rejects CPU
 *  stores, but the MPU rejection surfaces a bad write pointer as a
 *  MemManage fault immediately rather than a silent no-op.
 */
static void mpu_program_seg1_text(void) {
    uint32_t base = (uint32_t)&__flash_start;
    uint32_t end  = (uint32_t)&__flash_end - 1U;
    mpu_program_region(MPU_REGION_TEXT, base, end,
                       RP2350_MPU_RBAR_AP_RO_ANY, 0U /* exec OK */);
}

/**
 * @brief Program MPU region 2 — SEG2a (SRAM from base up to .uninit).
 *
 *  Covers .data, .bss, .mpu_diag, and any free SRAM below .uninit.
 *  RW+XN: the MemManage handler can write mpu_diag fields here, but no
 *  code can be executed from this range.  Keeping .mpu_diag inside an
 *  explicit RW region (rather than relying on PRIVDEFENA's RW+EXEC
 *  default) preserves defence-in-depth.
 */
static void mpu_program_seg2_sram_lo(void) {
    uint32_t base = (uint32_t)&__sram_start;
    uint32_t end  = (uint32_t)&__uninit_start - 1U;
    mpu_program_region(MPU_REGION_SRAM_LO, base, end,
                       RP2350_MPU_RBAR_AP_RW_ANY,
                       RP2350_MPU_RBAR_XN);
}

/**
 * @brief Compute the base address of the 32-byte stack-overflow guard.
 *
 *  The guard sits MPU_STACK_RESERVED_BYTES + MPU_STACK_GUARD_BYTES below
 *  __sram_end.  It is the boundary between SRAM_MID (RW+XN, kernel data)
 *  and SRAM_TOP (RW+XN, live stack).  A descending stack that exhausts its
 *  MPU_STACK_RESERVED_BYTES budget faults on the next push into the guard.
 *
 * @return Base address of the guard region.
 */
static inline uint32_t mpu_stack_guard_base(void) {
    return (uint32_t)&__sram_end -
           MPU_STACK_RESERVED_BYTES -
           MPU_STACK_GUARD_BYTES;
}

uint32_t tiku_stack_arch_bottom(void)
{
    return mpu_stack_guard_base() + MPU_STACK_GUARD_BYTES;
}

/**
 * @brief Program MPU region 3 — SEG2b (SRAM from .uninit end to guard base).
 *
 *  RW+XN.  In the current TikuOS configuration (static allocation only) this
 *  range is unused at runtime, but the region is still programmed so the W^X
 *  invariant holds for any future dynamic allocator.
 */
static void mpu_program_seg2_sram_mid(void) {
    uint32_t base = (uint32_t)&__uninit_end;
    uint32_t end  = mpu_stack_guard_base() - 1U;
    /* Defensive: if static data ever reached the guard base, base > end would
     * program an INVERTED region (RLAR limit below RBAR base) -- undefined on
     * ARMv8-M. The link-time ASSERT in rp2350.ld ((__stack - _end) >= 36 KB)
     * makes this impossible; this guards only the exact-granule boundary and
     * any future symbol drift. The range is empty when base > end, so leaving
     * region 3 disabled is correct -- the guard + SRAM_TOP still cover the
     * stack, and .uninit stays protected by region 0. */
    if (base > end) {
        return;
    }
    mpu_program_region(MPU_REGION_SRAM_MID, base, end,
                       RP2350_MPU_RBAR_AP_RW_ANY,
                       RP2350_MPU_RBAR_XN);
}

/**
 * @brief Program MPU region 4 — the 32-byte stack-overflow guard (RO+XN).
 *
 *  A descending stack that walks past MPU_STACK_RESERVED_BYTES of usage
 *  faults here via MemManage.  RO prevents silent stack-data corruption;
 *  XN prevents a write-then-branch from smuggling shellcode.
 */
static void mpu_program_stack_guard(void) {
    uint32_t base = mpu_stack_guard_base();
    uint32_t end  = base + MPU_STACK_GUARD_BYTES - 1U;
    mpu_program_region(MPU_REGION_STACK_GUARD, base, end,
                       RP2350_MPU_RBAR_AP_RO_ANY,
                       RP2350_MPU_RBAR_XN);
}

/**
 * @brief Program MPU region 5 — SEG2c (live stack region, RW+XN).
 *
 *  Covers from just above the guard to __sram_end.  Stack pushes succeed
 *  here; XN prevents a stack-buffer overflow from being turned into a
 *  code-injection exploit.
 */
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

/**
 * @brief Return the current software-bookkept SAM register value.
 *
 * @return 16-bit SAM word (SEG1/SEG2/SEG3 R/W/X bit fields).
 */
uint16_t tiku_mpu_arch_get_sam(void)   { return stub_mpusam; }

/**
 * @brief Write the SAM register and update hardware MPU protection.
 *
 *  Mirrors the MSP430 password-write sequence in the software register
 *  file.  Only the SEG3 W bit (bit 9) is forwarded to the ARMv8-M MPU
 *  hardware; SEG1 and SEG2 bits are bookkeeping only (see file header).
 *
 * @param sam  16-bit SAM value to apply.
 */
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

/**
 * @brief Return the software-bookkept MPUCTL0 control register value.
 *
 * @return 16-bit MPUCTL0 mirror (password | enable | SEGIE bits).
 */
uint16_t tiku_mpu_arch_get_ctl(void)   { return stub_mpuctl0; }

/**
 * @brief Disable all interrupts (PRIMASK on Cortex-M33).
 */
void tiku_mpu_arch_disable_irq(void) { tiku_cpu_irq_disable(); }

/**
 * @brief Re-enable all interrupts (clear PRIMASK on Cortex-M33).
 */
void tiku_mpu_arch_enable_irq(void)  { tiku_cpu_irq_enable(); }

/**
 * @brief Initialize all six MPU regions and enable the ARMv8-M MPU.
 *
 *  Detects cold boot via the mpu_diag magic sentinel and zeroes the
 *  diagnostic struct on first power-up.  Programs the six non-overlapping
 *  W^X regions (NVM, text, SRAM-lo, SRAM-mid, stack guard, SRAM-top),
 *  sets PRIVDEFENA so peripheral memory stays accessible, enables the
 *  MemManage exception at priority 0, and issues a final DSB+ISB.
 */
void tiku_mpu_arch_init_segments(void) {
    /* Cold-boot detection: if the magic word is missing the .mpu_diag
     * region is whatever random bytes were in SRAM at power-up. Zero
     * the struct and write magic. On warm reset (post-fault) the magic
     * is preserved and we keep the existing counters — that's how the
     * violation-detect test sees "yes the previous boot faulted". */
    if (mpu_diag.magic != TIKU_MPU_DIAG_MAGIC) {
        mpu_diag.magic             = TIKU_MPU_DIAG_MAGIC;
        mpu_diag.violation_count   = 0U;
        mpu_diag.last_fault_addr   = 0U;
        mpu_diag.last_fault_mmfsr  = 0U;
        mpu_diag.expect_fault      = 0U;
        mpu_diag.test_done_mask    = 0U;
        mpu_diag.hfnmi_phase       = 0U;
        mpu_diag.handler_misbehave = 0U;
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
     * burning more regions.
     *
     * ------------------------------------------------------------
     * HFNMIENA -- MPU during HardFault / NMI / FaultMask priorities
     * ------------------------------------------------------------
     *
     * Setting HFNMIENA = 1 makes the MPU active inside HardFault,
     * NMI, and any handler running at faultmask-priority. Leaving it
     * 0 (the TikuOS default) disables MPU enforcement in those
     * contexts. The choice is a deliberate trade between two
     * different failure modes:
     *
     *   HFNMIENA = 0 (default, this build):
     *       A bug in the HardFault/NMI handler that writes to a
     *       wrong address silently corrupts memory. The panic path
     *       never re-faults; it just trashes data and continues to
     *       the AIRCR reset. Easy to debug "why does my chip reset
     *       cleanly?"; hard to debug "why did this byte change?".
     *
     *   HFNMIENA = 1 (opt-in via TIKU_MPU_HFNMI_ENFORCE=1):
     *       A bug in the HardFault/NMI handler that does a
     *       MPU-faulting access escalates to Cortex-M LOCKUP.
     *       Lockup is unrecoverable except by external reset (the
     *       CPU literally stops fetching instructions). The MPU
     *       caught the bug; the cost is that you brick the chip
     *       until somebody power-cycles it.
     *
     * Why the default is OFF:
     *
     *   The current TikuOS HardFault/MemManage handlers do nothing
     *   that would MPU-fault under enforcement (audit in the block
     *   comment above tiku_rp2350_mem_fault_handler). But a future
     *   handler that adds, say, a stack-allocated buffer that grows
     *   past the stack guard, or a write to a debug log that lives
     *   in .uninit without the mpu_unlock_nvm bracket, would lock
     *   the chip up. Locking up is much harder to recover from in
     *   the field than a silent corruption + reset.
     *
     *   We pay the lower-protection price to keep the panic path
     *   survivable. Production hardening for security-critical
     *   builds (where lockup-on-fault is preferable to silent
     *   corruption) can flip this by passing
     *   -DTIKU_MPU_HFNMI_ENFORCE=1 at build time.
     *
     * When to revisit the default:
     *
     *   1. After running automated coverage on the fault path.
     *   2. After auditing every handler -- mem_fault, hard_fault,
     *      and any new arch-specific handler -- to confirm no
     *      MPU-protected access happens.
     *   3. When the build is shipping to a customer who'd rather
     *      a single bricked unit than a silently-corrupted fleet.
     * ------------------------------------------------------------
     */
#ifndef TIKU_MPU_HFNMI_ENFORCE
#define TIKU_MPU_HFNMI_ENFORCE 0
#endif

    {
        uint32_t ctrl_val = RP2350_MPU_CTRL_ENABLE
                          | RP2350_MPU_CTRL_PRIVDEFENA;
#if TIKU_MPU_HFNMI_ENFORCE
        ctrl_val |= RP2350_MPU_CTRL_HFNMIENA;
#endif
        _RP2350_REG(RP2350_MPU_CTRL) = ctrl_val;
    }
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

/**
 * @brief Return the cumulative MPU violation count across all warm boots.
 *
 * @return Number of MemManage or HardFault events recorded in mpu_diag.
 */
uint32_t tiku_mpu_arch_violation_count(void) {
    return mpu_diag.violation_count;
}

/**
 * @brief Return the MMFAR address captured during the most recent fault.
 *
 * @return Faulting address, or 0 if MMARVALID was not set.
 */
uint32_t tiku_mpu_arch_last_fault_addr(void) {
    return mpu_diag.last_fault_addr;
}

/**
 * @brief Return the full CFSR (MMFSR/BFSR/UFSR) from the most recent fault.
 *
 * @return 32-bit CFSR snapshot.
 */
uint32_t tiku_mpu_arch_last_fault_cfsr(void) {
    return mpu_diag.last_fault_cfsr;
}

/**
 * @brief Return the HFSR from the most recent fault.
 *
 *  Bit 30 (FORCED) set means the fault was escalated from a lower-priority
 *  configurable handler.
 *
 * @return 32-bit HFSR snapshot.
 */
uint32_t tiku_mpu_arch_last_fault_hfsr(void) {
    return mpu_diag.last_fault_hfsr;
}

/**
 * @brief Return the exception number active when the most recent fault fired.
 *
 *  3 = HardFault path, 4 = MemManage path (see ARMv8-M IPSR encoding).
 *
 * @return IPSR value captured inside the fault handler.
 */
uint32_t tiku_mpu_arch_last_fault_ipsr(void) {
    return mpu_diag.last_fault_ipsr;
}

/**
 * @brief Return the current expect_fault sentinel value.
 *
 *  0 = no fault expected; 1 = test armed; 2 = MemManage observed;
 *  3 = HardFault observed.
 *
 * @return Value of mpu_diag.expect_fault.
 */
uint32_t tiku_mpu_arch_test_expect_fault(void) {
    return mpu_diag.expect_fault;
}

/**
 * @brief Arm the test scaffold to expect an imminent MPU fault.
 *
 *  Sets expect_fault to 1.  The MemManage handler transitions it to 2
 *  (or HardFault handler to 3) so the post-reset boot can confirm that
 *  enforcement fired on the intended access.
 */
void tiku_mpu_arch_test_arm_fault(void) {
    /* Arm the test scaffold: the upcoming MPU fault is expected;
     * the MemManage handler will set this flag back to a "fault
     * was observed" sentinel after the reset. */
    mpu_diag.expect_fault = 1U;
}

/**
 * @brief Clear the violation counter and reset all fault diagnostic fields.
 *
 *  Zeroes violation_count, last_fault_addr, last_fault_mmfsr, and
 *  expect_fault.  Useful before a fresh test run to avoid carrying over
 *  state from a previous boot cycle.
 */
void tiku_mpu_arch_test_clear_violation(void) {
    mpu_diag.violation_count  = 0U;
    mpu_diag.last_fault_addr  = 0U;
    mpu_diag.last_fault_mmfsr = 0U;
    mpu_diag.expect_fault     = 0U;
}

/**
 * @brief Return the bitmask of W^X sub-tests that have already passed.
 *
 * @return test_done_mask (see TIKU_MPU_TEST_DONE_* bit definitions).
 */
uint32_t tiku_mpu_arch_test_done_mask(void) {
    return mpu_diag.test_done_mask;
}

/**
 * @brief Mark one W^X sub-test as done in the persistent bitmask.
 *
 * @param bit  One of the TIKU_MPU_TEST_DONE_* bit constants.
 */
void tiku_mpu_arch_test_mark_done(uint32_t bit) {
    mpu_diag.test_done_mask |= bit;
}

/**
 * @brief Clear the test_done_mask so all W^X sub-tests run again from scratch.
 */
void tiku_mpu_arch_test_clear_done_mask(void) {
    mpu_diag.test_done_mask = 0U;
}

/**
 * @brief Return the HFNMI-distinguish test phase counter.
 *
 *  0 = idle; 1 = armed (handler_misbehave set); 2 = bogus write attempted;
 *  3 = write completed without lockup (HFNMIENA=0 confirmed).
 *
 * @return Value of mpu_diag.hfnmi_phase.
 */
uint32_t tiku_mpu_arch_test_hfnmi_phase(void) {
    return mpu_diag.hfnmi_phase;
}

/**
 * @brief Arm the HFNMI-distinguish test scaffold.
 *
 *  Sets hfnmi_phase=1 and handler_misbehave=1.  On the next HardFault the
 *  handler will deliberately write to Region 1 (RO flash) to probe whether
 *  HFNMIENA=0 or =1 is in effect.
 */
void tiku_mpu_arch_test_hfnmi_arm(void) {
    mpu_diag.hfnmi_phase       = 1U;
    mpu_diag.handler_misbehave = 1U;
}

/**
 * @brief Clear the HFNMI-distinguish test scaffold state.
 *
 *  Resets hfnmi_phase and handler_misbehave to 0 so the handler returns to
 *  normal operation.
 */
void tiku_mpu_arch_test_hfnmi_clear(void) {
    mpu_diag.hfnmi_phase       = 0U;
    mpu_diag.handler_misbehave = 0U;
}

/**
 * @brief Restore the default W^X protection policy for all segments.
 *
 *  Equivalent to calling tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM).
 */
void tiku_mpu_arch_set_default_protection(void) {
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

/**
 * @brief Set the permission bits for one segment in the SAM register.
 *
 *  Updates the three-bit field for the given segment index inside the
 *  16-bit SAM word and calls tiku_mpu_arch_set_sam() to propagate the
 *  change to both the software mirror and (for SEG3) the hardware MPU.
 *
 * @param seg   Segment index (0 = SEG1, 1 = SEG2, 2 = SEG3).
 * @param perm  Three-bit permission value (R=bit0, W=bit1, X=bit2).
 */
void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm) {
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t mask  = (uint16_t)0x07U << shift;
    uint16_t sam   = stub_mpusam;

    sam = (uint16_t)((sam & ~mask) |
                     (((uint16_t)perm & 0x07U) << shift));
    tiku_mpu_arch_set_sam(sam);
}

/**
 * @brief Open an NVM write window by making the .uninit region writable.
 *
 *  Snapshots the current SAM word, ORs in the W bits for all three segments
 *  (matching MSP430 driver semantics), and reprograms the hardware MPU to
 *  RW for Region 0 (.uninit / SEG3).  The caller must pass the returned
 *  value to tiku_mpu_arch_lock_nvm() to restore protection.
 *
 * @return Previous SAM value to pass back to tiku_mpu_arch_lock_nvm().
 */
uint16_t tiku_mpu_arch_unlock_nvm(void) {
    /* Snapshot current SAM so caller can restore exactly via lock_nvm.
     * Then OR in the W bits across all three segments (matches MSP430
     * driver behaviour) and flip the hardware NVM region to RW. */
    uint16_t saved = stub_mpusam;
    stub_mpusam = (uint16_t)(saved | 0x0222U);
    mpu_set_nvm_ap(RP2350_MPU_RBAR_AP_RW_ANY);
    return saved;
}

/**
 * @brief Close the NVM write window and restore previous protection.
 *
 *  Restores the SAM word saved by tiku_mpu_arch_unlock_nvm() and
 *  reprograms the hardware MPU for Region 0 to the AP implied by the
 *  restored SEG3 W bit.
 *
 * @param saved_state  Value previously returned by tiku_mpu_arch_unlock_nvm().
 */
void tiku_mpu_arch_lock_nvm(uint16_t saved_state) {
    /* Restore the SAM word the caller stashed, and program the
     * hardware NVM region to whatever AP that implies for SEG3. */
    tiku_mpu_arch_set_sam(saved_state);
}

/**
 * @brief Return the software-bookkept MPUCTL1 violation flag register.
 *
 *  Bits mirror the MMFSR cause bits ORed in by the MemManage handler on
 *  each fault.
 *
 * @return 16-bit violation flag word (MPUCTL1 mirror).
 */
uint16_t tiku_mpu_arch_get_violation_flags(void) {
    return stub_mpuctl1;
}

/**
 * @brief Clear the software-bookkept violation flag register to zero.
 */
void tiku_mpu_arch_clear_violation_flags(void) {
    stub_mpuctl1 = 0U;
}

/**
 * @brief Enable the MemManage exception so MPU faults do not escalate.
 *
 *  On Cortex-M33 this sets SCB_SHCSR.MEMFAULTENA.  Without this, an MPU
 *  fault from Thread mode escalates to HardFault.  Also mirrors the
 *  MSP430 MPU_SEGIE bit in stub_mpuctl0.
 */
void tiku_mpu_arch_enable_violation_nmi(void) {
    /* On Cortex-M the closest equivalent to the MSP430 "violation
     * NMI" is enabling the MemManage exception so MPU faults vector
     * to MemManage_Handler instead of escalating to HardFault. */
    _RP2350_REG(RP2350_SCB_SHCSR) |= RP2350_SCB_SHCSR_MEMFAULTENA;
    mpu_dsb_isb();
    stub_mpuctl0 |= 0x0010U;            /* mirror MPU_SEGIE in bookkeeping */
}

/*---------------------------------------------------------------------------*/
/* MemManage handler — fault handler section                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief MemManage fault handler — strong override of the weak CRT alias.
 *
 *  Captures CFSR/HFSR/MMFAR and IPSR into mpu_diag (in .mpu_diag SRAM,
 *  accessible even under HFNMIENA=1), bumps the persistent violation counter,
 *  ORs MMFSR cause bits into the MSP430-style violation flag, and triggers a
 *  system reset via AIRCR.
 *
 *  MPU-safety audit (re-run whenever the handler body changes).  Every
 *  load/store must land in a region that would NOT fault if HFNMIENA=1:
 *    _RP2350_REG(SCB_*)      SCS 0xE000E000+  -- PRIVDEFENA RW
 *    mpu_diag.*              .mpu_diag SRAM   -- Region 2 RW + XN
 *    stub_mpuctl1            .bss SRAM        -- Region 2 RW + XN
 *    instruction fetch       .text flash      -- Region 1 RX
 *  If you add a UART log, debug breadcrumb, or .uninit write without the
 *  unlock bracket, update this audit and verify the new access is covered
 *  before enabling TIKU_MPU_HFNMI_ENFORCE.
 */
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

/**
 * @brief HardFault handler — strong override of the weak CRT alias.
 *
 *  Captures CFSR/HFSR/MMFAR and IPSR into mpu_diag and triggers a system
 *  reset via AIRCR.  The test scaffold uses expect_fault=3 (vs MemManage's
 *  =2) to tell the two fault paths apart on the post-reset boot.
 *
 *  MPU-safety audit (same checklist as MemManage handler):
 *    _RP2350_REG(SCB_*)   SCS 0xE000E000+  -- PRIVDEFENA RW
 *    mpu_diag.*           .mpu_diag SRAM   -- Region 2 RW + XN
 *    instruction fetch    .text flash      -- Region 1 RX
 *  If TIKU_MPU_HFNMI_ENFORCE is enabled, any new access that does not fit
 *  one of the three categories above will lock up the chip on every fault.
 *  Update this audit whenever the handler body is extended.
 *
 *  Exception (test scaffold only, gated by mpu_diag.handler_misbehave):
 *  When the HFNMI-distinguish test is armed, the handler deliberately
 *  writes to Region 1 (RO flash) before the AIRCR reset.  Under HFNMIENA=0
 *  the write is a silent no-op (XIP rejects stores physically); under
 *  HFNMIENA=1 the MPU enforces and the write faults inside the HF handler,
 *  escalating to Cortex-M Lockup.  WD_REASON on the next boot tells the
 *  test which path fired.  Production builds never set handler_misbehave.
 */
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

    /* HFNMI-distinguish scaffold: when armed, deliberately violate
     * Region 1 (write to .text) so HFNMIENA=0 vs HFNMIENA=1 produce
     * different reset causes. See the audit comment above. */
    if (mpu_diag.handler_misbehave != 0U) {
        /* Mark phase=2 ("about to attempt bogus write") so a chip
         * that locks up here still has a record of how far it got. */
        mpu_diag.hfnmi_phase = 2U;
        /* Address inside Region 1 (.text), well past our image. Write
         * never actually mutates XIP either way; the differentiator
         * is whether the MPU faults on the store cycle. */
        volatile uint32_t *p = (volatile uint32_t *)0x10100000UL;
        *p = 0xDEADBEEFU;
        /* Reaching here means HFNMIENA=0 (the write was a silent
         * no-op, MPU did not fire). Mark phase=3 ("write completed,
         * about to AIRCR"). */
        mpu_diag.hfnmi_phase = 3U;
    }

    _RP2350_REG(RP2350_SCB_AIRCR) =
        RP2350_SCB_AIRCR_VECTKEY | RP2350_SCB_AIRCR_SYSRESET;
    for (;;) { /* spin until reset asserts */ }
}
