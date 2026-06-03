/*
 * Tiku Operating System v0.05
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

/* No HIFRAM concept on RP2350; default SAM mirrors the "no HIFRAM"
 * MSP430 part value so any test that compares against
 * TIKU_MPU_DEFAULT_SAM passes on host-style runs. */
#define TIKU_MPU_DEFAULT_SAM    0x0555U

uint16_t tiku_mpu_arch_get_sam(void);
void     tiku_mpu_arch_set_sam(uint16_t sam);
uint16_t tiku_mpu_arch_get_ctl(void);
void     tiku_mpu_arch_disable_irq(void);
void     tiku_mpu_arch_enable_irq(void);
void     tiku_mpu_arch_init_segments(void);
void     tiku_mpu_arch_set_default_protection(void);
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);
uint16_t tiku_mpu_arch_unlock_nvm(void);
void     tiku_mpu_arch_lock_nvm(uint16_t saved_state);
uint16_t tiku_mpu_arch_get_violation_flags(void);
void     tiku_mpu_arch_clear_violation_flags(void);
void     tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_RP2350_MPU_ARCH_H_ */
