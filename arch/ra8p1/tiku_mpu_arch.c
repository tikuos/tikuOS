/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - RA8P1 memory protection, PMSAv8 on the Cortex-M85.
 *
 * W^X over the SRAM-resident image, a stack guard, and the NVM window that
 * brackets every durable write.  Region attributes also set cacheability.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_cache_arch.h"

/* Linker-provided boundaries; every one is 32-byte aligned by the script,
 * because PMSAv8 cannot express a finer region edge. */
extern uint32_t __vectors_start;
extern uint32_t _etext;
extern uint32_t __data_start;
extern uint32_t __uninit_start;
extern uint32_t __uninit_end;
extern uint32_t __stack;

/**
 * @brief Region indices.  Non-overlapping: overlap is implementation-defined.
 */
#define MPU_RGN_TEXT        0U   /* vectors + text + rodata: RO, executable */
#define MPU_RGN_DATA        1U   /* data + bss: RW, XN                      */
#define MPU_RGN_NVM         2U   /* .uninit: RO until the unlock window     */
#define MPU_RGN_FREE        3U   /* SRAM above .uninit up to the guard      */
#define MPU_RGN_GUARD       4U   /* RO trap under the stack                 */
#define MPU_RGN_STACK       5U   /* the live stack                          */
#define MPU_RGN_COUNT       6U

/*
 * Stack budget and guard width, taken from the RP2350 port's scars rather
 * than rediscovered: a 32-byte guard is LEAPT by a KB-sized frame -- SP lands
 * below it untouched and only a stray local inside the window ever faults.
 * A 4 KB guard cannot be jumped by any frame smaller than 4 KB.
 */
#define MPU_STACK_RESERVED_BYTES   32768U
#define MPU_STACK_GUARD_BYTES      4096U

/** @brief Software permission mirror; see tiku_mpu_arch_set_sam(). */
static uint16_t mpu_sam = TIKU_MPU_DEFAULT_SAM;

/** @brief Bookkeeping CTL, mirroring MSP430's MPUCTL0 password|enable. */
static uint16_t mpu_ctl;

/** @brief Violation record, latched by the MemManage handler. */
static uint16_t mpu_violations;

/** @brief Make an MPU register write visible before the next access. */
static inline void mpu_barrier(void)
{
    __asm__ volatile ("dsb 0xF" ::: "memory");
    __asm__ volatile ("isb 0xF" ::: "memory");
}

/**
 * @brief Program one region from a base/limit pair.
 *
 * @param rgn    Region number
 * @param base   First byte, 32-byte aligned
 * @param limit  Last byte + 1, 32-byte aligned
 * @param ap     RA8P1_MPU_RBAR_AP_RW or _AP_RO
 * @param xn     Non-zero to forbid execution
 * @param attr   MAIR index: RA8P1_MPU_ATTR_NORMAL or _DEVICE
 */
static void mpu_region(uint32_t rgn, uintptr_t base, uintptr_t limit,
                       uint32_t ap, int xn, uint32_t attr)
{
    if (limit <= base) {
        /* An empty span must be DISABLED, not programmed with a limit below
         * its base -- that encoding matches nothing and the range silently
         * falls through to the background map. */
        TIKU_REG32(RA8P1_MPU_RNR)  = rgn;
        TIKU_REG32(RA8P1_MPU_RLAR) = 0UL;
        return;
    }
    TIKU_REG32(RA8P1_MPU_RNR)  = rgn;
    TIKU_REG32(RA8P1_MPU_RBAR) = ((uint32_t)base & ~0x1FUL) | ap |
                                 (xn ? RA8P1_MPU_RBAR_XN : 0UL);
    TIKU_REG32(RA8P1_MPU_RLAR) = (((uint32_t)limit - 1UL) & ~0x1FUL) |
                                 RA8P1_MPU_RLAR_ATTR(attr) |
                                 RA8P1_MPU_RLAR_EN;
}

/** @brief Lowest address the stack may reach before it hits the guard. */
static uintptr_t guard_base(void)
{
    return (uintptr_t)&__stack - MPU_STACK_RESERVED_BYTES;
}

/**
 * @brief Point the NVM region at a new access permission.
 *
 * @param ap  RA8P1_MPU_RBAR_AP_RW while a durable write is bracketed,
 *            RA8P1_MPU_RBAR_AP_RO otherwise
 */
static void mpu_nvm_ap(uint32_t ap)
{
    mpu_region(MPU_RGN_NVM, (uintptr_t)&__uninit_start,
               (uintptr_t)&__uninit_end, ap, 1, RA8P1_MPU_ATTR_NORMAL);
    mpu_barrier();
}

void tiku_mpu_arch_init_segments(void)
{
    uint32_t dregion = (TIKU_REG32(RA8P1_MPU_TYPE) >>
                        RA8P1_MPU_TYPE_DREGION_SHIFT) &
                       RA8P1_MPU_TYPE_DREGION_MASK;

    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
    mpu_violations = 0U;

    /* Refuse rather than program a subset: a partial map leaves the gaps
     * falling through to the background region, which is the opposite of the
     * protection this is here to provide. */
    if (dregion < MPU_RGN_COUNT) {
        mpu_ctl = 0U;
        return;
    }

    TIKU_REG32(RA8P1_MPU_CTRL) = 0UL;
    mpu_barrier();

    TIKU_REG32(RA8P1_MPU_MAIR0) =
        (uint32_t)RA8P1_MPU_MAIR_NORMAL_WB |
        ((uint32_t)RA8P1_MPU_MAIR_DEVICE << 8);

    /* W^X: the image's text is read-only AND executable; everything else in
     * SRAM is writable AND never executable.  The split is exact because the
     * linker aligned _etext to the 32-byte granule. */
    mpu_region(MPU_RGN_TEXT, (uintptr_t)&__vectors_start, (uintptr_t)&_etext,
               RA8P1_MPU_RBAR_AP_RO, 0, RA8P1_MPU_ATTR_NORMAL);
    mpu_region(MPU_RGN_DATA, (uintptr_t)&__data_start,
               (uintptr_t)&__uninit_start,
               RA8P1_MPU_RBAR_AP_RW, 1, RA8P1_MPU_ATTR_NORMAL);
    mpu_nvm_ap(RA8P1_MPU_RBAR_AP_RO);
    mpu_region(MPU_RGN_FREE, (uintptr_t)&__uninit_end, guard_base(),
               RA8P1_MPU_RBAR_AP_RW, 1, RA8P1_MPU_ATTR_NORMAL);
    mpu_region(MPU_RGN_GUARD, guard_base(),
               guard_base() + MPU_STACK_GUARD_BYTES,
               RA8P1_MPU_RBAR_AP_RO, 1, RA8P1_MPU_ATTR_NORMAL);
    mpu_region(MPU_RGN_STACK, guard_base() + MPU_STACK_GUARD_BYTES,
               (uintptr_t)&__stack, RA8P1_MPU_RBAR_AP_RW, 1,
               RA8P1_MPU_ATTR_NORMAL);

    /* PRIVDEFENA keeps the default map alive underneath, so peripherals and
     * MRAM stay reachable without a region each.  HFNMIENA is deliberately
     * OFF: a fault handler that needs to write a diagnostic must not be
     * blocked by the very protection that faulted. */
    TIKU_REG32(RA8P1_MPU_CTRL) = RA8P1_MPU_CTRL_ENABLE |
                                 RA8P1_MPU_CTRL_PRIVDEFENA;
    mpu_barrier();

    TIKU_REG32(RA8P1_SCB_SHCSR) |= RA8P1_SCB_SHCSR_MEMFAULTENA;
    mpu_ctl = 0xA500U | 0x0001U;   /* password | enable, MSP430 parity */

    /* Caches LAST, and from here rather than from boot: MAIR is what makes
     * SRAM cacheable, so a cache enabled before the regions exist would run
     * on the default memory map's attributes and then need re-doing. */
    tiku_ra8p1_cache_enable();
}

uint16_t tiku_mpu_arch_get_sam(void)
{
    return mpu_sam;
}

void tiku_mpu_arch_set_sam(uint16_t sam)
{
    mpu_sam = sam;
}

uint16_t tiku_mpu_arch_get_ctl(void)
{
    return mpu_ctl;
}

void tiku_mpu_arch_disable_irq(void)
{
}

void tiku_mpu_arch_enable_irq(void)
{
}

void tiku_mpu_arch_set_default_protection(void)
{
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
    mpu_nvm_ap(RA8P1_MPU_RBAR_AP_RO);
}

void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)
{
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t mask  = (uint16_t)0x07U << shift;

    tiku_mpu_arch_set_sam((uint16_t)((mpu_sam & ~mask) |
                                     (((uint16_t)perm & 0x07U) << shift)));
}

uint16_t tiku_mpu_arch_unlock_nvm(void)
{
    uint16_t saved = mpu_sam;

    mpu_sam = (uint16_t)(saved | 0x0222U);
    mpu_nvm_ap(RA8P1_MPU_RBAR_AP_RW);
    return saved;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved_state)
{
    tiku_mpu_arch_set_sam(saved_state);
    /* Nest-safe: an inner lock inside a still-open outer window restores the
     * permission the SAVED state implies, not unconditionally RO. */
    mpu_nvm_ap((saved_state & 0x0222U) ? RA8P1_MPU_RBAR_AP_RW
                                       : RA8P1_MPU_RBAR_AP_RO);
}

uint16_t tiku_mpu_arch_get_violation_flags(void)
{
    return mpu_violations;
}

void tiku_mpu_arch_clear_violation_flags(void)
{
    mpu_violations = 0U;
}

void tiku_mpu_arch_enable_violation_nmi(void)
{
    mpu_ctl |= 0x0010U;   /* mirror MSP430's MPUSEGIE */
}

/**
 * @brief MemManage: record the violation.
 *
 * Records and returns, so a store that can never succeed re-executes forever:
 * this catches a stray write for a debugger, it does not RESUME past one.
 * Resumable handling is R8's fault-record work.
 */
void tiku_ra8p1_mem_fault_handler(void)
{
    uint32_t cfsr = TIKU_REG32(RA8P1_SCB_CFSR) & 0xFFUL;

    if (cfsr & (RA8P1_CFSR_DACCVIOL | RA8P1_CFSR_IACCVIOL)) {
        mpu_violations |= 0x0002U;   /* SEG1 in the MSP430 mirror */
    }
    TIKU_REG32(RA8P1_SCB_CFSR) = cfsr;   /* write-1-to-clear */
    mpu_barrier();
}
