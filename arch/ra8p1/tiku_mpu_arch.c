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
 * @brief Region indices.  Non-overlapping: overlap is implementation-defined.
 */
#define MPU_RGN_TEXT        0U   /* MRAM vectors + text + rodata: RO, exec  */
#define MPU_RGN_DATA        1U   /* data + bss: RW, XN, cacheable           */
#define MPU_RGN_NVM         2U   /* MRAM durable carve: RO outside the window */
#define MPU_RGN_FREE        3U   /* SRAM above .uninit up to the guard      */
#define MPU_RGN_GUARD       4U   /* RO trap under the stack                 */
#define MPU_RGN_STACK       5U   /* the live stack                          */
#define MPU_RGN_WARM        6U   /* .uninit: RW, XN, non-cacheable          */
#define MPU_RGN_COUNT       7U

/*
 * Stack budget and guard width: a 32-byte guard is LEAPT by a KB-sized frame
 * -- SP lands below it untouched and only a stray local inside the window ever
 * faults.  A 4 KB guard cannot be jumped by any frame smaller than 4 KB.
 */
#define MPU_STACK_RESERVED_BYTES   32768U
#define MPU_STACK_GUARD_BYTES      4096U

/** @brief Software permission mirror; see tiku_mpu_arch_set_sam(). */
static uint16_t mpu_sam = TIKU_MPU_DEFAULT_SAM;

/** @brief Bookkeeping CTL, mirroring MSP430's MPUCTL0 password|enable. */
static uint16_t mpu_ctl;

/** @brief Violation bits reported to the kernel; a fault resets before any
 *         handler can set them, so this only ever reads back as cleared. */
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
     * The whole reserved MRAM tail -- file store AND persist partition -- in one
     * region, because both need the same two things.
     *
     * Non-cacheable: this span IS the MRAM program path, and a D-cache line is
     * 32 bytes, exactly the program granule.  A cached store would settle in a
     * line instead of reaching the controller's buffer, then write the whole
     * granule back at an eviction the code never chose -- outside any window,
     * with the programming gate shut.
     *
     * Read-only outside the window: every write into either span goes through
     * a bracketed path (persist cells, and the region backend, which opens its
     * own window), so a store that arrives unbracketed is a bug and faults
     * here rather than quietly corrupting a file.
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

    /* Before the dregion check below, which can return early: the fault
     * handlers are worth arming even where the part offers too few regions
     * to program a map at all. */
    tiku_ra8p1_fault_init();

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
        ((uint32_t)RA8P1_MPU_MAIR_DEVICE << 8) |
        ((uint32_t)RA8P1_MPU_MAIR_NORMAL_NC << 16);

    /* W^X: the image's text is read-only AND executable; everything else is
     * writable AND never executable.  The split is exact because the linker
     * aligned _etext to the 32-byte granule.  The text side lives in MRAM,
     * which is writable non-volatile memory, so this region is also what
     * keeps a stray store from rewriting the program image. */
    mpu_region(MPU_RGN_TEXT, (uintptr_t)&__vectors_start, (uintptr_t)&_etext,
               RA8P1_MPU_RBAR_AP_RO, 0, RA8P1_MPU_ATTR_NORMAL);
    /* .data and .bss: ordinary cacheable SRAM.  This stops at __uninit_start
     * so the warm survivors can carry different attributes below. */
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
    /* .uninit and the RETAINED survivors, non-cacheable.  A reset discards dirty
     * lines, so cached warm state reaches SRAM only once something happens to
     * evict it, so the newest writes are the ones lost.  The grade promises
     * survival, so the attribute belongs here rather than in a cache
     * maintenance call every writer would have to remember.  It stays
     * writable and unbracketed: tiku_hang_boot_init() stores here at boot. */
    mpu_region(MPU_RGN_WARM, (uintptr_t)&__uninit_start,
               (uintptr_t)&__uninit_end, RA8P1_MPU_RBAR_AP_RW, 1,
               RA8P1_MPU_ATTR_NORMAL_NC);

    /* PRIVDEFENA keeps the default map alive underneath, so peripherals and
     * MRAM stay reachable without a region each.  HFNMIENA is deliberately
     * OFF: a fault handler that needs to write a diagnostic must not be
     * blocked by the very protection that faulted. */
    TIKU_REG32(RA8P1_MPU_CTRL) = RA8P1_MPU_CTRL_ENABLE |
                                 RA8P1_MPU_CTRL_PRIVDEFENA;
    mpu_barrier();

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

/*
 * TWO gates, not one.
 *
 * The MPU decides whether the store is allowed to issue; MRCPC1.MRCPSEN
 * decides whether the MRAM controller will program what the store put in its
 * buffer.  Opening only the MPU gives a store that raises no fault and
 * commits nothing -- which is exactly the silent-loss failure the durable
 * grade exists to prevent -- so both move together here.
 *
 * The close side also FLUSHES.  A store leaves its bytes in the controller's
 * 32-byte buffer, and a read returns the buffered value, so nothing the
 * caller can observe distinguishes "written" from "durable": if lock_nvm()
 * did not commit, a power cut after a apparently-successful write would lose
 * it with no diagnostic anywhere.
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
    /* Nest-safe: an inner lock inside a still-open outer window restores the
     * permission the SAVED state implies, not unconditionally RO -- and
     * leaves the programming gate open for the outer window to close. */
    if (!still_open) {
        tiku_ra8p1_mram_program_enable(0);
    }
    mpu_nvm_ap(still_open ? RA8P1_MPU_RBAR_AP_RW
                          : RA8P1_MPU_RBAR_AP_RO);
}

uint16_t tiku_mpu_arch_get_violation_flags(void)
{
    const tiku_ra8p1_fault_record_t *f = tiku_ra8p1_fault_last();

    /* The in-RAM latch cannot outlive the reset the fault handler now forces,
     * so the surviving evidence is the fault record: an access violation in it
     * IS the flag, and it is still there on the next boot. */
    if (f->magic == TIKU_RA8P1_FAULT_MAGIC &&
        f->kind == (uint32_t)TIKU_RA8P1_FAULT_MEM &&
        (f->cfsr & (RA8P1_CFSR_DACCVIOL | RA8P1_CFSR_IACCVIOL))) {
        return (uint16_t)(mpu_violations | 0x0002U);   /* SEG1, MSP430 mirror */
    }
    return mpu_violations;
}

void tiku_mpu_arch_clear_violation_flags(void)
{
    mpu_violations = 0U;
    /* The surviving half of the flag lives in the fault record, so clearing
     * only the RAM latch would leave get_violation_flags() still reporting a
     * violation from a previous boot. */
    tiku_ra8p1_fault_clear();
}

void tiku_mpu_arch_enable_violation_nmi(void)
{
    mpu_ctl |= 0x0010U;   /* mirror MSP430's MPUSEGIE */
}

/*
 * The MemManage handler lives in tiku_fault_arch.c: it dumps, records and
 * RESETS.  Returning from an access violation instead re-executes a store that
 * can never succeed, so a handler that records and returns spins forever.
 */
