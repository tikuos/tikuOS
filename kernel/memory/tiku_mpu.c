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
 * default is read+execute with no write, so code unlocks NVM, writes and
 * relocks; the fault-behaviour comment below lists what a stray store does.
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
 * @brief Initialize the MPU: the port's setup, then default protection.
 *
 * First the port's setup, tiku_mpu_arch_init_segments(), then the default
 * policy, TIKU_MPU_DEFAULT_SAM: read and execute without write on every
 * segment but segment 3 of MSP430 parts with HIFRAM.
 */
void tiku_mpu_init(void)
{
    tiku_mpu_arch_init_segments();
    tiku_mpu_arch_set_default_protection();
#if defined(TIKU_MPU_NMI_ON_VIOLATION) && TIKU_MPU_NMI_ON_VIOLATION
    /* With TIKU_MPU_NMI_ON_VIOLATION=1 (a debug build, set through
     * EXTRA_CFLAGS) an MSP430 store outside the NVM window raises the SYSNMI
     * violation handler; without it the store is dropped unreported (see the
     * fault-behaviour comment below). */
    tiku_mpu_enable_violation_nmi();
#endif
}

/**
 * @brief Make the loadable-module window executable or writable.
 *
 * Delegates to the arch layer; a no-op where the module runs in place.
 */
void tiku_mpu_module_window_exec(int enable)
{
    tiku_mpu_arch_module_window_exec(enable);
}

/**
 * @brief Set permissions on one segment.
 *
 * Delegates to the arch layer which handles the platform-specific
 * register encoding for setting per-segment permissions.
 */
void tiku_mpu_set_permissions(tiku_mpu_seg_t seg, tiku_mpu_perm_t perm)
{
    tiku_mpu_arch_set_seg_perm((uint8_t)seg, (uint8_t)perm);
}

/*
 * Unlock opens every segment at once, because durable data is not confined
 * to one segment.  tiku_mpu_set_permissions() changes a single segment.
 */

/*
 * Fault behaviour: what a durable store outside the NVM window does.
 *   MSP430              the MPU drops the write; nothing reports it unless
 *                       the violation NMI is enabled.
 *   nRF54L              precise bus fault: the RRAMC WEN gate is closed.
 *   RP2350, Ambiq,      MemManage fault; the handler records the violation
 *   RA8P1               in a record that survives the reset it then forces.
 *   STM32N6, ESP32-C61  nothing is enforced: the store lands in the SRAM
 *                       working copy and reaches the mirror only when a later
 *                       window closes.
 * On MSP430 a store that did not crash may not have landed.  Building with
 * TIKU_MPU_NMI_ON_VIOLATION=1 makes MSP430 report it as well.
 */

/**
 * @brief Unlock NVM for writing: adds write permission to all segments.
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
 * Flushes any in-RAM .persistent changes before re-locking.  Flushing at the
 * window's close catches both writes through the NVM helper and direct stores
 * into .persistent variables inside the window.
 */
void tiku_mpu_lock_nvm(uint16_t saved_state)
{
    /* The flush result is dropped; tiku_mpu_lock_nvm_status() returns it. */
    (void)tiku_mpu_lock_nvm_status(saved_state);
}

tiku_mem_err_t tiku_mpu_lock_nvm_status(uint16_t saved_state)
{
    int rc = tiku_mem_arch_nvm_flush_status();
    tiku_mpu_arch_lock_nvm(saved_state);
    return rc == 0 ? TIKU_MEM_OK : TIKU_MEM_ERR_IO;
}

/**
 * @brief Execute a function with NVM unlocked and interrupts disabled.
 *
 * While NVM is unlocked an ISR could write it too, so interrupts stay masked
 * for the whole window and only @p fn can write NVM.
 *
 * @param fn   Write function, called once inside the window
 * @param ctx  Passed to @p fn
 * @note Keep @p fn short: interrupts stay masked while it runs.  Split a long
 *       write into several scoped writes.  Interrupts are enabled on return,
 *       whatever their state on entry.
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

/**
 * @brief Raise an interrupt on an MPU violation where the platform can.
 *
 * On MSP430 a dropped store then raises SYSNMI, whose handler latches the
 * violated segment.  Other ports change nothing: they already fault on a
 * violation, or (STM32N6, ESP32-C61) enforce nothing.
 */
void tiku_mpu_enable_violation_nmi(void)
{
    tiku_mpu_arch_enable_violation_nmi();
}

/**
 * @brief Return the latched MPU violation flags.
 *
 * The violation handler (MSP430's SYSNMI, or a port's fault handler) latches
 * the flags in software; this hands them to the VFS, shell and tests.  Ports
 * that latch nothing return 0.
 *
 * @return Violation flags in the port's encoding, one bit per segment on
 *         MSP430; zero when none has been recorded since the last clear.
 * @see tiku_mpu_clear_violation_flags()
 */
uint16_t tiku_mpu_get_violation_flags(void)
{
    return tiku_mpu_arch_get_violation_flags();
}

/**
 * @brief Clear the recorded violation flags.
 *
 * Resets the latched violation record so that subsequent calls
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
 * Snapshot of MMFAR on RP2350 and Ambiq, preserved across the post-fault
 * reset.  Returns 0 on other ports or before any fault.
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
