/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - RP2350 MPU driver (ARMv8-M enforcement).
 *
 * Six non-overlapping regions give W^X coverage plus a stack-overflow guard,
 * with .uninit standing in for NVM: read-only until an explicit unlock window.
 * A MemManage fault latches the address, counts the violation and resets.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "tiku_rp2350_regs.h"
#include "tiku_cpu_freq_boot_arch.h"
#include <hal/tiku_cpu.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* LINKER SYMBOLS FOR THE PROTECTED REGIONS                                  */
/*---------------------------------------------------------------------------*/

/* The region bounds come from these linker symbols, so the regions follow
 * the code and data layout as sections grow. */

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;
extern uint32_t __sram_start;
extern uint32_t __sram_end;
extern uint32_t __flash_start;
extern uint32_t __flash_end;

/*---------------------------------------------------------------------------*/
/* SOFTWARE MPU REGISTER FILE                                                */
/*---------------------------------------------------------------------------*/

/* The kernel MPU API uses the MSP430 MPU's register model (MPUCTL0/1, SAM,
 * SEGB1/2); these variables hold it, so a read returns what was written.  The
 * hardware follows only SEG3's W bit, on region 0 (.uninit); the other regions
 * keep the permissions tiku_mpu_arch_init_segments() gave them. */

/** @brief Software MPUCTL0: the password, enable and SEGIE bits. */
static uint16_t stub_mpuctl0;
static uint16_t stub_mpusam = TIKU_MPU_DEFAULT_SAM;
static uint16_t stub_mpusegb1;
static uint16_t stub_mpusegb2;

/*---------------------------------------------------------------------------*/
/* PERSISTENT DIAGNOSTIC STATE                                               */
/*---------------------------------------------------------------------------*/

/* The .mpu_diag NOLOAD section sits below .uninit, outside the MPU-protected
 * range: the MemManage handler writes it while .uninit is read-only, and it
 * survives the reset the handler triggers, so the next boot can read the
 * fault.  After a power-up its bytes are random; the magic word marks valid
 * state, and tiku_mpu_arch_init_segments() zeroes the struct when it is
 * missing. */

/** @brief Magic sentinel for cold-boot detection in .mpu_diag . */
#define TIKU_MPU_DIAG_MAGIC  0x4D505551U

/** @brief Persistent MPU diagnostic state that survives warm reset.
 *
 *  Placed in the .mpu_diag NOLOAD section, outside the MPU-protected .uninit
 *  range, so the MemManage handler can write diagnostics while executing in an
 *  MPU-fault context.  Without the magic word the struct is power-up SRAM.
 */
struct tiku_mpu_diag {
    uint32_t magic;             /**< TIKU_MPU_DIAG_MAGIC when valid */
    uint32_t violation_count;   /**< MPU faults counted across warm boots */
    uint32_t last_fault_addr;   /**< MMFAR from the most recent fault */
    uint32_t last_fault_mmfsr;  /**< MMFSR cause bits from the most recent
                                     fault (byte 0 of CFSR). */
    uint32_t expect_fault;      /**< Test scaffold: 1 = armed, 2 = fault seen
                                     by MemManage, 3 = seen by HardFault */
    uint32_t last_fault_cfsr;   /**< Full CFSR (MMFSR/BFSR/UFSR) snapshot. */
    uint32_t last_fault_hfsr;   /**< HFSR snapshot; bit 30 (FORCED) means the
                                     fault escalated to HardFault */
    uint32_t last_fault_ipsr;   /**< IPSR of the handler that took the
                                     fault (4 = MemManage, 3 = HardFault) */
    /** Bitmask of the W^X sub-tests verified in the current run: bit 0 =
      * SEG3 write, 1 = SEG1 write, 2 = SEG2 exec, 3 = stack guard.  Each
      * boot skips a set bit and arms the next test; the scaffold clears it. */
    uint32_t test_done_mask;
    /** HFNMI-distinguish test phase counter.
     *  0 = idle / fresh; 1 = armed (waiting for fault);
     *  2 = "bogus write attempted"; 3 = "write completed, AIRCR pending". */
    uint32_t hfnmi_phase;
    /** Test scaffold flag: when 1, the HardFault handler stores to flash
     *  before issuing AIRCR, to tell HFNMIENA=0 (store dropped) from
     *  HFNMIENA=1 (lockup).  Only tiku_mpu_arch_test_hfnmi_arm() sets it. */
    uint32_t handler_misbehave;
    uint32_t violation_flags;  /**< MMFSR flags retained across warm reset. */
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
 *  Sits outside the ARMv8-M MPU-protected .uninit range so the MemManage and
 *  HardFault handlers can write to it unconditionally.  volatile, because
 *  handlers at exception priority modify it outside normal call paths.
 */
__attribute__((section(".mpu_diag")))
static volatile struct tiku_mpu_diag mpu_diag;

/*---------------------------------------------------------------------------*/
/* HARDWARE MPU HELPERS                                                      */
/*---------------------------------------------------------------------------*/

/** @brief ARMv8-M MPU region index assignments.
 *
 *  Regions do not overlap: on ARMv8-M an access that hits two regions
 *  faults.  The SRAM range above .uninit splits into three so a RO+XN guard
 *  can sit at the bottom of the stack.
 */
#define MPU_REGION_NVM         0U   /* SEG3 = .uninit (RO/RW + XN) */
#define MPU_REGION_TEXT        1U   /* SEG1 = flash (.text + .rodata, RX) */
#define MPU_REGION_SRAM_LO     2U   /* SEG2a = SRAM below uninit (RW + XN) */
#define MPU_REGION_SRAM_MID    3U   /* SEG2b = SRAM above uninit, below
                                       stack guard (RW + XN) */
#define MPU_REGION_STACK_GUARD 4U   /* SG: MPU_STACK_GUARD_BYTES RO+XN
                                       guard at the bottom of the
                                       descending stack; a stack store
                                       that lands in it faults via
                                       MemManage */
#define MPU_REGION_SRAM_TOP    5U   /* SEG2c = SRAM above the guard, up
                                       to __sram_end (RW + XN); this is
                                       the live stack region */

/** @brief Stack reserve and stack-guard sizes.
 *
 *  Every process runs on the one main stack (no PSP).  A frame larger than
 *  the guard can step over it without a fault, so the 4 KB guard exceeds
 *  every frame in the tree; the 32 KB reserve holds BASIC's deepest nesting.
 *
 * @note rp2350.ld (0x9000) and TikuBench tests/memory/test_mem_mpu.c hold
 *       copies of these values; change all three together.
 */
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
     *   bits[ 3:1]  AttrIndx    - index into MAIR0/MAIR1
     *   bit[0]      EN          - region enable
     *
     * LIMIT is the inclusive end address with its low 5 bits cleared.
     * Those bits are not address bits: setting them selects a MAIR1
     * attribute, Device-nGnRnE at reset, under which SRAM accesses can
     * hang. */
    uint32_t end          = (uint32_t)&__uninit_end;
    uint32_t limit_field  = (end - 1U) & ~0x1FU;
    return limit_field
         | (0U << 1)               /* AttrIndx = 0 -> MAIR0[7:0] */
         | RP2350_MPU_RLAR_EN;
}

/**
 * @brief Reprogram MPU region 0 (.uninit / SEG3) with new AP bits.
 *
 * @note The RNR / RBAR / RLAR sequence is not atomic: mask any interrupt
 *       that also programs the MPU before calling.
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
 *  and SH=Non-shareable.  The low 5 bits of both addresses are cleared, as
 *  the 32-byte MPU granule requires.
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
 *  Flash is mapped read-execute only.  XIP flash takes no CPU stores; with
 *  this region a store through a bad pointer raises a MemManage fault.
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
 *  Covers .data, .bss, .mpu_diag and any free SRAM below .uninit, as RW+XN: the
 *  MemManage handler can write mpu_diag fields, but no code executes from this
 *  range.
 */
static void mpu_program_seg2_sram_lo(void) {
    uint32_t base = (uint32_t)&__sram_start;
    uint32_t end  = (uint32_t)&__uninit_start - 1U;
    mpu_program_region(MPU_REGION_SRAM_LO, base, end,
                       RP2350_MPU_RBAR_AP_RW_ANY,
                       RP2350_MPU_RBAR_XN);
}

/**
 * @brief Compute the base address of the stack-overflow guard.
 *
 *  The guard sits MPU_STACK_RESERVED_BYTES + MPU_STACK_GUARD_BYTES below
 *  __sram_end, on the boundary between SRAM_MID (kernel data) and SRAM_TOP
 *  (live stack), so a descending stack that exhausts its budget faults.
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
 *  RW+XN.  The range holds the .retained variables and the SRAM tier.
 */
static void mpu_program_seg2_sram_mid(void) {
    uint32_t base = (uint32_t)&__uninit_end;
    uint32_t end  = mpu_stack_guard_base() - 1U;
    /* With base above end the region would be inverted (limit below base),
     * which ARMv8-M leaves undefined, so region 3 is left unprogrammed; the
     * guard and SRAM_TOP still cover the stack.  The link-time ASSERT in
     * rp2350.ld keeps _end 36 KB below __stack, so this happens only when
     * .uninit ends exactly at the guard base. */
    if (base > end) {
        return;
    }
    mpu_program_region(MPU_REGION_SRAM_MID, base, end,
                       RP2350_MPU_RBAR_AP_RW_ANY,
                       RP2350_MPU_RBAR_XN);
}

/**
 * @brief Program MPU region 4 — the stack-overflow guard (RO+XN).
 *
 *  A descending stack that walks past MPU_STACK_RESERVED_BYTES of usage
 *  faults here via MemManage: RO refuses the store, XN refuses execution.
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
 *  here; XN stops code written onto the stack from running.
 */
static void mpu_program_seg2_sram_top(void) {
    uint32_t base = mpu_stack_guard_base() + MPU_STACK_GUARD_BYTES;
    uint32_t end  = (uint32_t)&__sram_end - 1U;
    mpu_program_region(MPU_REGION_SRAM_TOP, base, end,
                       RP2350_MPU_RBAR_AP_RW_ANY,
                       RP2350_MPU_RBAR_XN);
}

/*---------------------------------------------------------------------------*/
/* HAL IMPLEMENTATION                                                        */
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
 *  Stores @p sam in the software register file, with the MSP430 model's
 *  MPUCTL0 password write.  Only the SEG3 W bit (bit 9) reaches the
 *  hardware, as region 0's AP; the SEG1 and SEG2 bits are stored only.
 *
 * @param sam  16-bit SAM value to apply.
 */
void     tiku_mpu_arch_set_sam(uint16_t sam) {
    stub_mpuctl0 = 0xA500U;             /* MPUCTL0 password write */
    stub_mpusam  = sam;
    stub_mpuctl0 = 0xA500U | 0x0001U;   /* password | enable */

    /* SEG3's W bit (bit 9 of SAM) sets region 0's AP: RW when set, RO
     * when clear. */
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
 *  Detects cold boot via the mpu_diag magic sentinel and zeroes the diagnostic
 *  struct, programs the six non-overlapping W^X regions (NVM, text, SRAM-lo,
 *  SRAM-mid, stack guard, SRAM-top), then enables the MPU and MemManage.
 */
void tiku_mpu_arch_init_segments(void) {
    /* No magic: the .mpu_diag bytes are power-up SRAM, so zero the struct
     * and write the magic.  A warm reset keeps the magic and the counters,
     * so the boot after a fault reads what the handler recorded. */
    if (mpu_diag.magic != TIKU_MPU_DIAG_MAGIC) {
        mpu_diag.violation_flags = 0U;
        mpu_diag.magic             = TIKU_MPU_DIAG_MAGIC;
        mpu_diag.violation_count   = 0U;
        mpu_diag.last_fault_addr   = 0U;
        mpu_diag.last_fault_mmfsr  = 0U;
        mpu_diag.last_fault_cfsr   = 0U;
        mpu_diag.last_fault_hfsr   = 0U;
        mpu_diag.last_fault_ipsr   = 0U;
        mpu_diag.expect_fault      = 0U;
        mpu_diag.test_done_mask    = 0U;
        mpu_diag.hfnmi_phase       = 0U;
        mpu_diag.handler_misbehave = 0U;
    }

    /* SEGB1/SEGB2 hold the MSP430 model's segment boundaries, 0x8000 and
     * 0xC000 shifted right by 4; no hardware uses them on this port. */
    stub_mpusegb1 = 0x0800U;   /* 0x8000 >> 4 */
    stub_mpusegb2 = 0x0C00U;   /* 0xC000 >> 4 */

    /* MAIR0 attribute 0 = Normal Non-cacheable, the attribute every region
     * uses. */
    _RP2350_REG(RP2350_MPU_MAIR0) = (uint32_t)RP2350_MPU_MAIR_NORMAL_NC;

    /* The MPU is off while the regions change, so no access meets a
     * half-programmed region. */
    _RP2350_REG(RP2350_MPU_CTRL) = 0U;
    mpu_dsb_isb();

    /* Region 0 = .uninit, default RO (locked). */
    mpu_set_nvm_ap(RP2350_MPU_RBAR_AP_RO_ANY);

    /* Region 1 = flash (.text + .rodata): RX. */
    mpu_program_seg1_text();

    /* Regions 2/3/4/5 = SRAM split into four pieces:
     *   2: low SRAM up to .uninit (data + bss + mpu_diag, RW + XN)
     *   3: mid SRAM after .uninit, up to the stack guard (RW + XN)
     *   4: MPU_STACK_GUARD_BYTES stack guard at the bottom of the
     *      descending stack (RO + XN) -- a stack push that walks past
     *      MPU_STACK_RESERVED_BYTES of usage faults here
     *   5: live stack region above the guard (RW + XN)
     * The pieces do not overlap, since an access that hits two ARMv8-M
     * regions faults.  Code written to SRAM faults when executed
     * anywhere in regions 2/3/5, which all carry XN. */
    mpu_program_seg2_sram_lo();
    mpu_program_seg2_sram_mid();
    mpu_program_stack_guard();
    mpu_program_seg2_sram_top();

    /* Enable the MPU with PRIVDEFENA: memory no region covers
     * (peripherals at 0x40000000+, the SCS at 0xE000E000+, XIP cache
     * control, the boot ROM) keeps the default privileged map.
     *
     * HFNMIENA is set only when TIKU_MPU_HFNMI_ENFORCE=1.  With it clear,
     * the MPU does not check HardFault, NMI or FAULTMASK-priority code: a
     * bad store in those handlers corrupts memory and the handler goes on
     * to its reset.  With it set, an MPU fault in those handlers locks the
     * core up until an external reset.  The fault handlers below list the
     * accesses they make; each is one the regions allow. */
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

    /* MemManage at priority 0, the highest configurable (SHPR1 byte 0,
     * 0xE000ED18), and enabled below, so an MPU fault from Thread mode is
     * taken as MemManage (IPSR 4) and not escalated to HardFault
     * (HFSR.FORCED, IPSR 3). */
    *(volatile uint8_t *)0xE000ED18U = 0U;

    /* MemManage is enabled here at init;
     * tiku_mpu_arch_enable_violation_nmi() sets the same bit. */
    _RP2350_REG(RP2350_SCB_SHCSR) |= RP2350_SCB_SHCSR_MEMFAULTENA;
    mpu_dsb_isb();
}

/*---------------------------------------------------------------------------*/
/* TEST-SCAFFOLD API                                                         */
/*---------------------------------------------------------------------------*/

/* Accessors for mpu_diag, which is file-scope.  The MPU tests (for example
 * test_mpu_violation_detect) arm a fault, let the handler reset the chip and
 * check the counters on the next boot. */

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
 * The MemManage handler stores MMFAR only when MMARVALID is set and keeps
 * the previous value otherwise; the HardFault handler always stores it.
 *
 * @return The recorded MMFAR value.
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
    /* The fault handler moves 1 to 2 (MemManage) or 3 (HardFault) before
     * it resets the chip. */
    mpu_diag.expect_fault = 1U;
}

/**
 * @brief Clear the violation counter and the main fault diagnostic fields.
 *
 * Zeroes the count, address, status snapshots and expected-fault marker.
 */
void tiku_mpu_arch_test_clear_violation(void) {
    mpu_diag.violation_count  = 0U;
    mpu_diag.last_fault_addr  = 0U;
    mpu_diag.last_fault_mmfsr = 0U;
    mpu_diag.last_fault_cfsr  = 0U;
    mpu_diag.last_fault_hfsr  = 0U;
    mpu_diag.last_fault_ipsr  = 0U;
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
 *  handler stores to region 1 (RO flash); whether that store faults shows
 *  whether HFNMIENA is set.
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

/*
 * Only SEG3's W bit reaches the hardware.  SEG1 and SEG2 keep their fixed
 * permissions, flash RX and SRAM RW+XN, and a call naming them changes only
 * the SAM bookkeeping.  XIP flash takes no stores, and an executable writable
 * SRAM region would let a stack overflow run injected code.
 */

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
 *  Saves the current SAM word, sets the W bit of all three segments in it
 *  (0x0222) and makes region 0 (.uninit / SEG3) RW.
 *
 * @return Previous SAM value to pass back to tiku_mpu_arch_lock_nvm().
 */
uint16_t tiku_mpu_arch_unlock_nvm(void) {
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
    return mpu_diag.violation_flags;
}

/**
 * @brief Clear the software-bookkept violation flag register to zero.
 */
void tiku_mpu_arch_clear_violation_flags(void) {
    mpu_diag.violation_flags = 0U;
}

/**
 * @brief Enable the MemManage exception so MPU faults do not escalate.
 *
 *  On Cortex-M33 this sets SCB_SHCSR.MEMFAULTENA.  Without this, an MPU
 *  fault from Thread mode escalates to HardFault.  Also mirrors the
 *  MSP430 MPU_SEGIE bit in stub_mpuctl0.
 */
void tiku_mpu_arch_enable_violation_nmi(void) {
    _RP2350_REG(RP2350_SCB_SHCSR) |= RP2350_SCB_SHCSR_MEMFAULTENA;
    mpu_dsb_isb();
    stub_mpuctl0 |= 0x0010U;            /* mirror MPU_SEGIE in bookkeeping */
}

/*---------------------------------------------------------------------------*/
/* FAULT HANDLERS                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief MemManage fault handler — strong override of the weak CRT alias.
 *
 *  Captures CFSR/HFSR/MMFAR and IPSR into mpu_diag (in .mpu_diag SRAM, which
 *  the regions leave writable), bumps the persistent violation counter, ORs
 *  MMFSR cause bits into the MPUCTL1 mirror, and resets via AIRCR.
 */
void tiku_rp2350_mem_fault_handler(void) {
    /* Every access in this handler is one the regions allow:
     *   _RP2350_REG(SCB_*)  SCS 0xE000E000+  -- PRIVDEFENA RW
     *   mpu_diag.*          .mpu_diag SRAM   -- Region 2 RW + XN
     *   mpu_diag.violation_flags  .mpu_diag SRAM -- Region 2 RW + XN
     *   instruction fetch   .text flash      -- Region 1 RX
     * The MPU checks this handler, so a refused access here, such as a
     * .uninit write outside the unlock window, escalates to HardFault. */
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

    /* Count the fault (the counter survives the reset below), OR the
     * MMFSR cause bits into the MPUCTL1 mirror, and move an armed
     * expect_fault to 2, which the next boot reads. */
    mpu_diag.violation_count++;
    mpu_diag.violation_flags |= (uint16_t)mmfsr;
    if (mpu_diag.expect_fault == 1U) {
        mpu_diag.expect_fault = 2U;   /* observed */
    }

    /* Clear the latched fault status (W1C). */
    _RP2350_REG(RP2350_SCB_CFSR) = cfsr;

    /* Reset the system.  Returning would re-run the faulting access, and
     * skipping it needs the instruction's length.  After the reset,
     * /sys/boot/mpu/violations and the MPU tests read the counter. */
    _RP2350_REG(RP2350_SCB_AIRCR) =
        RP2350_SCB_AIRCR_VECTKEY | RP2350_SCB_AIRCR_SYSRESET;

    /* Ensure the reset takes effect before any further code runs. */
    mpu_dsb_isb();
    for (;;) { /* spin until reset asserts */ }
}

/**
 * @brief HardFault handler — strong override of the weak CRT alias.
 *
 *  Captures CFSR/HFSR/MMFAR and IPSR into mpu_diag and resets via AIRCR.  The
 *  test scaffold uses expect_fault=3, against MemManage's 2, to tell the two
 *  fault paths apart on the post-reset boot.
 *
 * @note With mpu_diag.handler_misbehave set, which only the HFNMI test arms,
 *       the handler first stores to region 1 (RO flash).  With HFNMIENA=0 the
 *       store is dropped, as XIP flash takes no stores; with HFNMIENA=1 it
 *       faults and locks the core up.  The next boot's reset cause tells
 *       the two apart.
 */
void tiku_rp2350_hard_fault_handler(void) {
    /* Every access in this handler is one the regions allow:
     *   _RP2350_REG(SCB_*)  SCS 0xE000E000+  -- PRIVDEFENA RW
     *   mpu_diag.*          .mpu_diag SRAM   -- Region 2 RW + XN
     *   instruction fetch   .text flash      -- Region 1 RX
     * With TIKU_MPU_HFNMI_ENFORCE=1, any other access here locks the core
     * up on every fault. */
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

    /* HFNMI test: a store to region 1 (RO flash).  HFNMIENA=0 drops it;
     * HFNMIENA=1 faults and locks the core up, which ends in a different
     * reset cause. */
    if (mpu_diag.handler_misbehave != 0U) {
        /* Mark phase=2 ("about to attempt bogus write") so a chip
         * that locks up here still has a record of how far it got. */
        mpu_diag.hfnmi_phase = 2U;
        /* An address inside region 1 (flash), beyond the code image.  The
         * store cannot change flash; the MPU either faults on it or not. */
        volatile uint32_t *p = (volatile uint32_t *)0x10100000UL;
        *p = 0xDEADBEEFU;
        /* Reaching here means HFNMIENA=0: the store was dropped and the
         * MPU did not fault.  Mark phase=3 ("write completed, about to
         * AIRCR"). */
        mpu_diag.hfnmi_phase = 3U;
    }

    _RP2350_REG(RP2350_SCB_AIRCR) =
        RP2350_SCB_AIRCR_VECTKEY | RP2350_SCB_AIRCR_SYSRESET;
    for (;;) { /* spin until reset asserts */ }
}

/**
 * @brief Does nothing: a module on this port runs XIP from flash, so there is
 *        no RAM execution window to open or close.
 *
 * @param enable  Ignored.
 */
void tiku_mpu_arch_module_window_exec(int enable)
{
    (void)enable;
}
