/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu.c - MPU write-protection wrappers (platform-independent).
 *
 * Orchestration only; every register access goes through tiku_mpu_arch_*.  The
 * default is read+execute with no write, so code explicitly unlocks NVM, writes
 * and relocks -- see tiku_mpu_unlock_nvm() for what an unbracketed store does.
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
void tiku_mpu_module_window_exec(int enable)
{
    tiku_mpu_arch_module_window_exec(enable);
}

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

/*
 * FAULT-BEHAVIOR CONTRACT -- what an UNBRACKETED durable store does:
 *   MSP430          the FRAM MPU silently DROPS the write -- no fault, no flag
 *                   (unless the violation NMI is armed).  The quietest, and so
 *                   the most dangerous, failure mode in the fleet.
 *   nRF54L          precise BUS FAULT (RRAMC WEN closed) -- the loud canary.
 *   RP2350 / Ambiq  MemManage fault -> deliberate reset, with a persistent
 *                   .mpu_diag violation record.
 * Same bug, three behaviours: never read "it did not crash" as proof a durable
 * write landed on MSP430.  Bench builds should arm TIKU_MPU_NMI_ON_VIOLATION so
 * every platform fails loudly.
 */

/**
 * @brief Unlock NVM for writing -- adds write permission to all segments.
 *
 * @return Previous protection state for later restoration.
 */
uint16_t tiku_mpu_unlock_nvm(void)
{
    return tiku_mpu_arch_unlock_nvm();
}

/**
 * @brief Restore the MPU to a previously saved state.
 *
 * Flushes any in-RAM .persistent changes before re-locking.  Doing it HERE, at
 * the transaction boundary, catches both writes through the NVM helper and
 * direct stores into .persistent variables inside the window.
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
 * The ISR latches the flags into a software variable when a protected write is
 * detected; this hands that value to the VFS, shell and tests.
 *
 * @return Bitmask of violated segments (platform-specific encoding).
 *         Zero means no violations have been recorded since last clear.
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
 * Delegates to the arch layer's persistent counter, which lives in a section
 * that survives a fault-triggered reset where the platform has one, and reads
 * 0 where it does not.
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
