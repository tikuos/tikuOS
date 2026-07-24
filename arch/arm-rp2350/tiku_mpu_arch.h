/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - RP2350 MPU driver interface
 *
 * Programs the ARMv8-M MPU on the Cortex-M33 with six non-
 * overlapping regions:
 *   0: SEG3   .uninit (NVM stand-in)        -- RO/RW + XN
 *   1: SEG1   flash 0x10000000..end         -- RX
 *   2: SEG2a  SRAM 0x20000000..uninit_start -- RW + XN
 *   3: SEG2b  SRAM uninit_end..guard_base   -- RW + XN
 *   4: SG     32-byte stack-overflow guard  -- RO + XN
 *   5: SEG2c  SRAM above guard..sram_end    -- RW + XN
 * Together they give W^X across the whole address space and a
 * stack-overflow detector at the bottom of the descending stack.
 *
 * The kernel-level SAM bookkeeping API (set_permissions on
 * SEG1/2/3) is preserved for parity with MSP430 -- SAM bits track
 * in software so tests pass unchanged -- but only SEG3 actually
 * flows through to hardware permission changes (the
 * unlock/lock-NVM handshake). SEG1/SEG2 hardware permissions are
 * pinned: making flash writable or SRAM executable would brick
 * the kernel on this architecture.
 *
 * Persistent diagnostic state lives in a .mpu_diag NOLOAD section
 * that survives the AIRCR.SYSRESET this driver triggers on every
 * MemManage violation. See arch/arm-rp2350/tiku_mpu_arch.c for the
 * fault handler, the test scaffold, and the W^X violation tests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_MPU_ARCH_H_
#define TIKU_RP2350_MPU_ARCH_H_

#include <stdint.h>
#include "tiku.h"

/**
 * @brief Default software-SAM value used when no HIFRAM tier exists.
 *
 * On RP2350 there is no HIFRAM concept.  The constant mirrors the
 * "no HIFRAM" MSP430 SAM value so cross-platform tests that compare
 * against TIKU_MPU_DEFAULT_SAM pass unchanged.
 */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

/**
 * @brief Read the kernel's software segment-access-map (SAM) register.
 *
 * The SAM is maintained in software to track logical permission state
 * across SEG1/SEG2/SEG3 for parity with the MSP430 port.  On RP2350
 * only SEG3 (the .uninit NVM stand-in) flows through to real hardware
 * permission changes.
 *
 * @return Current SAM value.
 */
uint16_t tiku_mpu_arch_get_sam(void);

/**
 * @brief Write the kernel's software SAM register.
 *
 * @param sam  New SAM value.
 */
void     tiku_mpu_arch_set_sam(uint16_t sam);

/**
 * @brief Read the ARMv8-M MPU_CTRL register.
 *
 * @return Raw MPU_CTRL value (ENABLE, HFNMIENA, PRIVDEFENA bits).
 */
uint16_t tiku_mpu_arch_get_ctl(void);

/**
 * @brief Disable IRQs (PRIMASK = 1) — used to bracket NVM windows.
 */
void     tiku_mpu_arch_disable_irq(void);

/**
 * @brief Re-enable IRQs (PRIMASK = 0).
 */
void     tiku_mpu_arch_enable_irq(void);

/**
 * @brief Program all six ARMv8-M MPU regions from linker-symbol bounds.
 *
 * Configures W^X protection across flash, SRAM, and the .uninit NVM
 * region, plus a 32-byte stack-overflow guard at the bottom of the
 * descending stack.  Called once from tiku_mpu_arch_init_segments().
 */
void     tiku_mpu_arch_init_segments(void);

/**
 * @brief Apply the default NVM write-protection policy.
 *
 * Sets SEG3 (.uninit) to read-only and enables the MemManage handler.
 * Called at the end of tiku_mpu_init() after segment init.
 */
void     tiku_mpu_arch_set_default_protection(void);

/**
 * @brief Set logical permissions on a kernel segment (SEG1/SEG2/SEG3).
 *
 * Updates the software SAM and, for SEG3 only, adjusts the hardware
 * MPU region.  SEG1 (flash) and SEG2 (SRAM) hardware permissions are
 * pinned — making flash writable or SRAM executable would brick the
 * kernel on Cortex-M33.
 *
 * @param seg   Segment index (0 = SEG1, 1 = SEG2, 2 = SEG3).
 * @param perm  Permission bitmask (TIKU_MPU_READ / WRITE / EXEC).
 */
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Unlock the NVM (.uninit) region for writing.
 *
 * Adds write permission to the SEG3 MPU region and saves the prior
 * MPU_CTRL state so it can be restored by tiku_mpu_arch_lock_nvm().
 *
 * @return Saved MPU state to pass to tiku_mpu_arch_lock_nvm().
 */
uint16_t tiku_mpu_arch_unlock_nvm(void);

/**
 * @brief Restore MPU state and flush NVM after a write window.
 *
 * Re-applies write-protection to SEG3 using the saved state, then
 * calls tiku_mem_arch_nvm_flush() to commit the SRAM .uninit image to
 * the flash mirror sector.
 *
 * @param saved_state  Value returned by tiku_mpu_arch_unlock_nvm().
 */
void     tiku_mpu_arch_lock_nvm(uint16_t saved_state);

/**
 * @brief Read the persistent MemManage violation flags.
 *
 * The flags are stored in the .mpu_diag NOLOAD section and survive
 * the AIRCR.SYSRESET triggered on every MemManage fault.  Bit 0
 * indicates a violation was recorded; higher bits are reserved.
 *
 * @return Violation flags word.
 */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/**
 * @brief Clear the persistent MemManage violation flags.
 */
void     tiku_mpu_arch_clear_violation_flags(void);

/**
 * @brief Enable the MemManage handler in SCB.SHCSR.
 *
 * By default, MemManage faults escalate to HardFault.  Enabling the
 * MemManage handler lets the dedicated handler capture the faulting
 * address and violation count before triggering SYSRESET.
 */
void     tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_RP2350_MPU_ARCH_H_ */
