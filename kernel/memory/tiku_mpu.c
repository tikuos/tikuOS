/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
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
#include "hal/tiku_cpu.h"

/*---------------------------------------------------------------------------*/
/* MPU FUNCTIONS                                                             */
/*---------------------------------------------------------------------------*/

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

void tiku_mpu_module_window_exec(int enable)
{
    tiku_mpu_arch_module_window_exec(enable);
}

void tiku_mpu_set_permissions(tiku_mpu_seg_t seg, tiku_mpu_perm_t perm)
{
    tiku_mpu_arch_set_seg_perm((uint8_t)seg, (uint8_t)perm);
}

/*
 * Fault behaviour: what a durable store outside the NVM window does.  Each
 * port's tiku_mpu_arch.c decides it.
 *   MSP430              the MPU drops the write; nothing reports it unless
 *                       the violation NMI is enabled.
 *   nRF54L              precise bus fault: the RRAMC WEN gate is closed.
 *   RP2350, Ambiq,      MemManage fault; the handler records the violation
 *   RA8P1               in a record that survives the reset it then forces.
 *   ESP32-C5            store access fault (PMA entries 10 and 11 hold the
 *                       durable span read-only); the trap handler records it.
 *   STM32N6, ESP32-C61, nothing is enforced: the store lands in the SRAM
 *   ESP32-P4            working copy and reaches the mirror only when a later
 *                       window closes.
 * On MSP430 a store that did not crash may not have landed.  Building with
 * TIKU_MPU_NMI_ON_VIOLATION=1 makes MSP430 report it as well.
 */

uint16_t tiku_mpu_unlock_nvm(void)
{
    return tiku_mpu_arch_unlock_nvm();
}

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

void tiku_mpu_scoped_write(tiku_mpu_write_fn fn, void *ctx)
{
    uint16_t saved;

    tiku_atomic_enter();
    saved = tiku_mpu_unlock_nvm();

    fn(ctx);

    tiku_mpu_lock_nvm(saved);
    tiku_atomic_exit();
}

/*---------------------------------------------------------------------------*/
/* VIOLATION DETECTION                                                       */
/*---------------------------------------------------------------------------*/

void tiku_mpu_enable_violation_nmi(void)
{
    tiku_mpu_arch_enable_violation_nmi();
}

uint16_t tiku_mpu_get_violation_flags(void)
{
    return tiku_mpu_arch_get_violation_flags();
}

void tiku_mpu_clear_violation_flags(void)
{
    tiku_mpu_arch_clear_violation_flags();
}

uint32_t tiku_mpu_get_violation_count(void)
{
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ)
    extern uint32_t tiku_mpu_arch_violation_count(void);
    return tiku_mpu_arch_violation_count();
#else
    return 0U;
#endif
}

uint32_t tiku_mpu_get_last_fault_addr(void)
{
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ)
    extern uint32_t tiku_mpu_arch_last_fault_addr(void);
    return tiku_mpu_arch_last_fault_addr();
#else
    return 0U;
#endif
}
