/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - RA8P1 memory protection, PMSAv8 on the Cortex-M85.
 *
 * W^X over the MRAM-resident image, a stack guard, and the NVM window that
 * brackets every durable write.  Region attributes also set cacheability.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include <hal/tiku_cpu.h>
#include "tiku_ra8p1_regs.h"
#include "tiku_cache_arch.h"
#include "tiku_mram_arch.h"
#include "tiku_fault_arch.h"

/* Linker-provided boundaries; every one is 32-byte aligned by the script,
 * because PMSAv8 cannot express a finer region edge. */
extern uint32_t __vectors_start;
extern uint32_t _etext;
extern uint32_t __data_start;
extern uint32_t __uninit_start;
extern uint32_t __uninit_end;
extern uint32_t __stack;
extern uint32_t __tiku_nvmfs_base;
extern uint32_t __tiku_nvm_mram_end;

/**
 * @brief Region indices.  The regions are disjoint: PMSAv8 faults an access
 *        that matches two regions.
 */
#define MPU_RGN_TEXT        0U   /* MRAM vectors + text + rodata: RO, exec  */
#define MPU_RGN_DATA        1U   /* data + bss: RW, XN, cacheable           */
#define MPU_RGN_NVM         2U   /* MRAM NVM + persist: RO outside a window */
#define MPU_RGN_FREE        3U   /* SRAM above .uninit up to the guard      */
#define MPU_RGN_GUARD       4U   /* RO trap under the stack                 */
#define MPU_RGN_STACK       5U   /* the live stack                          */
#define MPU_RGN_WARM        6U   /* .uninit: RW, XN, non-cacheable          */
#define MPU_RGN_COUNT       7U

/*
 * Stack reserve below __stack, with the guard at its bottom.  A frame larger
 * than the guard can move SP past it without touching it; a 4 KB guard
 * catches every frame smaller than 4 KB.
 */
#define MPU_STACK_RESERVED_BYTES   32768U
#define MPU_STACK_GUARD_BYTES      4096U

/** @brief Software permission mirror; see tiku_mpu_arch_set_sam(). */
static uint16_t mpu_sam = TIKU_MPU_DEFAULT_SAM;

/** @brief Control word in MSP430 MPUCTL0 form, for tiku_mpu_arch_get_ctl(). */
static uint16_t mpu_ctl;

/** @brief RAM violation latch.  Nothing sets it: a violation resets the
 *         part, and the flag comes from the fault record. */
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
 * @param attr   MAIR index: RA8P1_MPU_ATTR_NORMAL, _NORMAL_NC or _DEVICE
 */
static void mpu_region(uint32_t rgn, uintptr_t base, uintptr_t limit,
                       uint32_t ap, int xn, uint32_t attr)
{
    if (limit <= base) {
        /* An empty span disables the region; a limit below the base would
         * match nothing. */
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

/** @brief Base of the 4 KB guard; the stack may not descend below its top. */
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
    /*
     * One region covers the MRAM NVM region and the persist partition.  It
     * is non-cacheable: a cached store would wait in a 32-byte line, the MRAM
     * program granule, and reach the controller only at a later eviction,
     * outside any write window.  It is read-only outside a window, so an
     * unbracketed store faults.
     */
    mpu_region(MPU_RGN_NVM, (uintptr_t)&__tiku_nvmfs_base,
               (uintptr_t)&__tiku_nvm_mram_end, ap, 1,
               RA8P1_MPU_ATTR_NORMAL_NC);
    mpu_barrier();
}

void tiku_mpu_arch_init_segments(void)
{
    uint32_t dregion = (TIKU_REG32(RA8P1_MPU_TYPE) >>
                        RA8P1_MPU_TYPE_DREGION_SHIFT) &
                       RA8P1_MPU_TYPE_DREGION_MASK;

    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
    mpu_violations = 0U;

    /* Enable the fault handlers before the region-count check, which can
     * return early. */
    tiku_ra8p1_fault_init();

    /* With fewer than MPU_RGN_COUNT regions nothing is programmed, and the
     * MPU and the caches stay off; a partial map would leave its gaps on the
     * background map. */
    if (dregion < MPU_RGN_COUNT) {
        mpu_ctl = 0U;
        return;
    }

    TIKU_REG32(RA8P1_MPU_CTRL) = 0UL;
    mpu_barrier();

    TIKU_REG32(RA8P1_MPU_MAIR0) =
        (uint32_t)RA8P1_MPU_MAIR_NORMAL_WB |
        ((uint32_t)RA8P1_MPU_MAIR_DEVICE << 8) |
        ((uint32_t)RA8P1_MPU_MAIR_NORMAL_NC << 16);

    /* W^X: vectors, text and rodata are read-only and executable; every other
     * region is execute-never.  The linker script aligns _etext to 32 bytes.
     * The text lies in writable MRAM, so this region also stops a stray store
     * from changing the image. */
    mpu_region(MPU_RGN_TEXT, (uintptr_t)&__vectors_start, (uintptr_t)&_etext,
               RA8P1_MPU_RBAR_AP_RO, 0, RA8P1_MPU_ATTR_NORMAL);
    /* .data and .bss: cacheable SRAM, up to __uninit_start; .uninit gets its
     * own region (MPU_RGN_WARM). */
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
    /* .uninit and TIKU_RETAINED data, non-cacheable: a reset discards dirty
     * lines, which would lose the newest writes.  The region is writable
     * without a window; tiku_hang_boot_init() writes it at boot. */
    mpu_region(MPU_RGN_WARM, (uintptr_t)&__uninit_start,
               (uintptr_t)&__uninit_end, RA8P1_MPU_RBAR_AP_RW, 1,
               RA8P1_MPU_ATTR_NORMAL_NC);

    /* PRIVDEFENA keeps the default map under the regions, so peripherals and
     * MRAM need no region of their own.  HFNMIENA is clear: the HardFault and
     * NMI handlers run with the MPU bypassed, so a fault handler can always
     * write its record. */
    TIKU_REG32(RA8P1_MPU_CTRL) = RA8P1_MPU_CTRL_ENABLE |
                                 RA8P1_MPU_CTRL_PRIVDEFENA;
    mpu_barrier();

    mpu_ctl = 0xA500U | 0x0001U;   /* password | enable, MSP430 parity */

    /* Caches last: with the MPU on, the region attributes decide what is
     * cached, which keeps the NVM span and .uninit out of the D-cache. */
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

void tiku_mpu_arch_disable_irq(void) {
    tiku_cpu_irq_disable();
}

void tiku_mpu_arch_enable_irq(void) {
    tiku_cpu_irq_enable();
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

/*
 * Two gates open and close together: the MPU decides whether a store to the
 * NVM span may issue, and MRCPC1.MRCPSEN whether the MRAM controller programs
 * what the store left in its buffer.
 *
 * The lock also flushes.  A store waits in the controller's 32-byte buffer,
 * and reads return the buffered value, so only the flush makes the write
 * durable.
 */
uint16_t tiku_mpu_arch_unlock_nvm(void)
{
    uint16_t saved = mpu_sam;

    mpu_sam = (uint16_t)(saved | 0x0222U);
    mpu_nvm_ap(RA8P1_MPU_RBAR_AP_RW);
    tiku_ra8p1_mram_program_enable(1);
    return saved;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved_state)
{
    int still_open = (saved_state & 0x0222U) != 0U;

    /* Commit before revoking anything: MRCFL is itself a controller write. */
    (void)tiku_ra8p1_mram_flush();

    tiku_mpu_arch_set_sam(saved_state);
    /* Nesting: an inner lock inside an open outer window restores the saved
     * permission (writable) and leaves the programming gate open for the
     * outer lock to close. */
    if (!still_open) {
        tiku_ra8p1_mram_program_enable(0);
    }
    mpu_nvm_ap(still_open ? RA8P1_MPU_RBAR_AP_RW
                          : RA8P1_MPU_RBAR_AP_RO);
}

uint16_t tiku_mpu_arch_get_violation_flags(void)
{
    const tiku_ra8p1_fault_record_t *f = tiku_ra8p1_fault_last();

    /* A violation resets the part, so the flag comes from the fault record,
     * which survives the reset: a MemManage record with DACCVIOL or IACCVIOL
     * reports SEG1. */
    if (f->magic == TIKU_RA8P1_FAULT_MAGIC &&
        f->kind == (uint32_t)TIKU_RA8P1_FAULT_MEM &&
        (f->cfsr & (RA8P1_CFSR_DACCVIOL | RA8P1_CFSR_IACCVIOL))) {
        return (uint16_t)(mpu_violations | 0x0001U);   /* SEG1 */
    }
    return mpu_violations;
}

void tiku_mpu_arch_clear_violation_flags(void)
{
    mpu_violations = 0U;
    /* The flag comes from the fault record, so the record is cleared too. */
    tiku_ra8p1_fault_clear();
}

void tiku_mpu_arch_enable_violation_nmi(void)
{
    mpu_ctl |= 0x0010U;   /* mirror MSP430's MPUSEGIE */
}

/*
 * The MemManage handler is in tiku_fault_arch.c: it prints, records and
 * resets, since returning would re-execute the faulting access.
 */
