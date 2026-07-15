/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu.c - MPU write-protection wrappers (platform-independent)
 *
 * Provides a controlled interface for NVM protection using the MPU.
 * All hardware register access and bit manipulation is routed through
 * the arch-level functions (tiku_mpu_arch_set_default_protection,
 * tiku_mpu_arch_set_seg_perm, tiku_mpu_arch_unlock_nvm, etc.), so
 * this file contains only platform-independent orchestration logic.
 *
 * Default policy: all MPU segments are read+execute, no write.
 * This prevents stray pointers and runaway code from corrupting NVM.
 * To write to NVM, code explicitly unlocks, writes, and relocks.
 *
 * FAULT-BEHAVIOR CONTRACT (what an UNBRACKETED durable store does):
 *   MSP430   the FRAM MPU silently DROPS the write — no fault, no flag
 *            (unless the violation NMI below is armed).  The quietest
 *            and therefore most dangerous failure mode in the fleet.
 *   nRF54L   precise BUS FAULT (RRAMC WEN closed) — the loud canary.
 *   RP2350 / Ambiq   MemManage fault -> deliberate reset, with a
 *            persistent .mpu_diag violation record.
 * Same bug, three behaviors: never rely on "it didn't crash" as proof
 * a durable write landed on MSP430.  Debug/bench builds should arm the
 * violation NMI (TIKU_MPU_NMI_ON_VIOLATION below) so all platforms
 * fail loudly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_mem.h"

/*---------------------------------------------------------------------------*/
/* MPU FUNCTIONS                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize MPU — configure boundaries and default protection
 *
 * First sets up the segment boundaries so the MPU knows which
 * address ranges belong to each segment, then sets the default
 * protection policy (read+execute, no write on all segments).
 */
void tiku_mpu_init(void)
{
    tiku_mpu_arch_init_segments();
    tiku_mpu_arch_set_default_protection();
#if defined(TIKU_MPU_NMI_ON_VIOLATION) && TIKU_MPU_NMI_ON_VIOLATION
    /* Debug/bench builds (EXTRA_CFLAGS="-DTIKU_MPU_NMI_ON_VIOLATION=1"):
     * make MSP430's silently-dropped unbracketed durable writes fire the
     * SYSNMI violation handler instead — parity with the loud failure
     * modes on the ARM ports (see the contract in the header above). */
    tiku_mpu_enable_violation_nmi();
#endif
}

/**
 * @brief Set permissions on one segment
 *
 * Delegates to the arch layer which handles the platform-specific
 * register encoding for setting per-segment permissions.
 */
void tiku_mpu_set_permissions(tiku_mpu_seg_t seg, tiku_mpu_perm_t perm)
{
    tiku_mpu_arch_set_seg_perm((uint8_t)seg, (uint8_t)perm);
}

/*
 * Why unlock is coarse-grained (all segments at once):
 *   Most NVM write operations need to touch multiple segments (code
 *   constants in one, data in another). Unlocking all at once keeps the
 *   critical section short. For finer control, use tiku_mpu_set_permissions()
 *   on individual segments.
 */

/**
 * @brief Unlock NVM for writing — adds write permission to all segments
 *
 * @return Previous protection state for later restoration
 */
uint16_t tiku_mpu_unlock_nvm(void)
{
    return tiku_mpu_arch_unlock_nvm();
}

/**
 * @brief Restore MPU to a previously saved state
 *
 * Before re-locking, flush any in-RAM .persistent modifications to
 * non-volatile storage.  On MSP430 this is a no-op (FRAM is already
 * durable); on RP2350 it triggers the flash-sector commit so the
 * unlock window's writes survive a full power cycle, not just a warm
 * reset.  Placing the flush HERE -- at the natural transaction
 * boundary -- catches both writes via tiku_mem_arch_nvm_write() and
 * direct stores into .persistent variables (memset, struct
 * assignments) inside the unlock window.
 */
void tiku_mpu_lock_nvm(uint16_t saved_state)
{
    tiku_mem_arch_nvm_flush();
    tiku_mpu_arch_lock_nvm(saved_state);
}

/*
 * Why scoped_write disables interrupts:
 *   While NVM is unlocked, any ISR that fires has write access to
 *   NVM. A bug in an ISR could corrupt persistent data. By disabling
 *   interrupts for the duration of the unlock window, we guarantee that
 *   only the caller's function can write to NVM.
 *
 * Caveat: the write function (fn) must be short — while it runs,
 * interrupts are masked and hardware events queue up. Long writes
 * should be broken into multiple scoped_write calls.
 */

/**
 * @brief Execute a function with NVM unlocked and interrupts disabled
 */
void tiku_mpu_scoped_write(tiku_mpu_write_fn fn, void *ctx)
{
    uint16_t saved;

    tiku_mpu_arch_disable_irq();
    saved = tiku_mpu_unlock_nvm();

    fn(ctx);

    tiku_mpu_lock_nvm(saved);
    tiku_mpu_arch_enable_irq();
}

/*---------------------------------------------------------------------------*/
/* VIOLATION DETECTION                                                       */
/*---------------------------------------------------------------------------*/

/*
 * On platforms with hardware MPU, violations may default to a device
 * reset. Enabling the violation NMI switches to an interrupt instead,
 * so the CPU can continue and software can inspect which segment was
 * violated.
 */

/** @brief Enable NMI-on-violation (instead of device reset). */
void tiku_mpu_enable_violation_nmi(void)
{
    tiku_mpu_arch_enable_violation_nmi();
}

/**
 * @brief Return the latched MPU violation flags from the NMI ISR.
 *
 * When the MPU detects a write to a protected region, the NMI ISR
 * latches the violation flags into a software variable.  This function
 * returns that latched value for diagnostic use (VFS `/sys/boot/mpu/violations`,
 * shell, tests).
 *
 * @return Bitmask of violated segments (platform-specific encoding).
 *         Zero means no violations have been recorded since last clear.
 *
 * @see tiku_mpu_clear_violation_flags()
 */
uint16_t tiku_mpu_get_violation_flags(void)
{
    return tiku_mpu_arch_get_violation_flags();
}

/**
 * @brief Clear both the software latch and hardware violation flags.
 *
 * Resets the NMI-latched violation record so that subsequent calls
 * to tiku_mpu_get_violation_flags() return zero until a new violation
 * occurs.
 *
 * @see tiku_mpu_get_violation_flags()
 */
void tiku_mpu_clear_violation_flags(void)
{
    tiku_mpu_arch_clear_violation_flags();
}

/**
 * @brief Total MPU violations across warm boots.
 *
 * Delegates to the arch layer which returns the persistent counter
 * (in a NOLOAD section that survives a fault-triggered reset on
 * platforms that have one).  Returns 0 on platforms without
 * persistent diagnostic state.
 */
uint32_t tiku_mpu_get_violation_count(void)
{
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ)
    extern uint32_t tiku_mpu_arch_violation_count(void);
    return tiku_mpu_arch_violation_count();
#else
    return 0U;
#endif
}

/**
 * @brief Address that triggered the most recent MPU violation.
 *
 * Snapshot of MMFAR on Cortex-M (or equivalent on other arches),
 * preserved across the post-fault reset. Returns 0 on platforms
 * without persistent diagnostic state or before any fault.
 */
uint32_t tiku_mpu_get_last_fault_addr(void)
{
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ)
    extern uint32_t tiku_mpu_arch_last_fault_addr(void);
    return tiku_mpu_arch_last_fault_addr();
#else
    return 0U;
#endif
}
