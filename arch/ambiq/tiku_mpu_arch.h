/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.h - Apollo 510 MPU driver interface
 *
 * Cortex-M55 has the ARMv8-M MPU (same RBAR/RLAR/MAIR/CTRL as the M33).
 * tiku_mpu_arch.c programs a real W^X layout (code RX, every data region XN,
 * plus a stack-overflow guard), re-pinned to the Apollo memory map. The NVM
 * region (.uninit) stays RW+XN because the NVM tier pool shares it, so the
 * SEG3 unlock/lock are SAM bookkeeping that still drive the mem-port-C MRAM
 * flush through the generic layer. See the .c file's header for the rationale.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_MPU_ARCH_H_
#define TIKU_AMBIQ_MPU_ARCH_H_

#include <stdint.h>
#include "tiku.h"

/**
 * @brief Default SAM (Segment Access Mask) value for this platform.
 *
 * Mirrors the RP2350 "no HIFRAM" default so portable tests that
 * compare against TIKU_MPU_DEFAULT_SAM behave identically across
 * arch ports.
 */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

/**
 * @brief Read the current Segment Access Mask from the arch layer.
 *
 * The SAM is an abstract encoding of the three MPU segment permissions
 * (read/write/execute) maintained by the generic mem layer. The arch
 * layer maps it to the ARMv8-M RBAR/RLAR register set.
 *
 * @return Current SAM value.
 */
uint16_t tiku_mpu_arch_get_sam(void);

/**
 * @brief Write a new Segment Access Mask to the arch layer.
 *
 * Applies the encoded permissions to the ARMv8-M MPU region registers.
 *
 * @param sam  New SAM value to apply.
 */
void     tiku_mpu_arch_set_sam(uint16_t sam);

/**
 * @brief Read the ARMv8-M MPU_CTRL register value.
 *
 * Used by the generic layer to inspect the MPU enable bit and
 * PRIVDEFENA / HFNMIENA flags.
 *
 * @return Current MPU_CTRL value.
 */
uint16_t tiku_mpu_arch_get_ctl(void);

/**
 * @brief Disable the MPU MemManage / fault interrupt.
 *
 * Called before entering a critical section that intentionally
 * accesses a protected region, preventing an unwanted fault escalation.
 */
void     tiku_mpu_arch_disable_irq(void);

/**
 * @brief Re-enable the MPU MemManage / fault interrupt.
 *
 * Called after exiting a critical section to restore normal MPU
 * fault signalling.
 */
void     tiku_mpu_arch_enable_irq(void);

/**
 * @brief Program all MPU regions for the Apollo510 memory map.
 *
 * Sets up the W^X layout: code region as RX, all data regions as
 * RW+XN, and a stack-overflow guard page at the bottom of DTCM.
 * Called once during boot from tiku_mpu_init().
 */
void     tiku_mpu_arch_init_segments(void);

/**
 * @brief Apply the default read+execute (no-write) protection to NVM.
 *
 * Locks all MPU segments back to the default policy after an
 * intentional write window. Called by tiku_mpu_lock_nvm().
 */
void     tiku_mpu_arch_set_default_protection(void);

/**
 * @brief Set the access permissions for a single MPU segment.
 *
 * @param seg   Segment index (0-based; maps to an ARMv8-M MPU region).
 * @param perm  Permission flags (combination of TIKU_MPU_* values).
 */
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Unlock NVM for writing and return the previous SAM state.
 *
 * Adds write permission to the NVM MPU region. The caller must restore
 * the state via tiku_mpu_arch_lock_nvm() as soon as the write is done.
 *
 * @return SAM value before unlocking (pass to tiku_mpu_arch_lock_nvm()).
 */
uint16_t tiku_mpu_arch_unlock_nvm(void);

/**
 * @brief Restore MPU NVM protection to the saved state.
 *
 * @param saved_state  Value returned by a prior tiku_mpu_arch_unlock_nvm().
 */
void     tiku_mpu_arch_lock_nvm(uint16_t saved_state);

/**
 * @brief Read MPU violation (fault) flags.
 *
 * Returns per-segment violation bits captured by the MemManage or
 * HardFault handler. A non-zero value means at least one unauthorized
 * access was detected since the last clear.
 *
 * @return Bitmask of violation flags (bits [2:0] for segments 0..2).
 */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/**
 * @brief Clear all MPU violation flags.
 *
 * Resets the fault-flag snapshot to zero so the next violation can
 * be cleanly detected.
 */
void     tiku_mpu_arch_clear_violation_flags(void);

/**
 * @brief Configure the MPU to generate an NMI on violation.
 *
 * Switches the MemManage fault response from a system reset to an NMI
 * so that the fault handler can record diagnostic state before any
 * reboot. Must be called before any intentional violation testing.
 */
void     tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_AMBIQ_MPU_ARCH_H_ */
