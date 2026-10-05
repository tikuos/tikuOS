/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_hal.h - per-port MPU functions and their platform routing.
 *
 * Includes the active platform's tiku_mpu_arch.h and declares the MPU
 * functions every port implements, on MSP430's model of three NVM segments
 * with a segment-access-mode (SAM) register.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MPU_HAL_H_
#define TIKU_MPU_HAL_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* PLATFORM ROUTING                                                          */
/*---------------------------------------------------------------------------*/

#if defined(PLATFORM_MSP430)
#include "arch/msp430/tiku_mpu_arch.h"
#elif defined(PLATFORM_RP2350)
#include "arch/arm-rp2350/tiku_mpu_arch.h"
#elif defined(PLATFORM_AMBIQ)
#include "arch/ambiq/tiku_mpu_arch.h"
#elif defined(PLATFORM_NORDIC)
#include "arch/nordic/tiku_mpu_arch.h"
#elif defined(PLATFORM_STM32N6)
#include "arch/stm32n6/tiku_mpu_arch.h"
#elif defined(PLATFORM_RA8P1)
#include "arch/ra8p1/tiku_mpu_arch.h"
#elif defined(PLATFORM_ESP32C61)
#include "arch/esp32c61/tiku_mpu_arch.h"
#endif

/*---------------------------------------------------------------------------*/
/* LOW-LEVEL REGISTER ACCESS (DIAGNOSTIC AND TEST USE)                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the segment access word, in the layout of MSP430's MPUSAM.
 *
 * MSP430 parts with an MPU return MPUSAM itself and parts without one return
 * 0; every other port returns a word it keeps in software.
 *
 * @return Current segment access word
 */
uint16_t tiku_mpu_arch_get_sam(void);

/**
 * @brief Set the segment access word, in the layout of MSP430's MPUSAM.
 *
 * MSP430 parts with an MPU write MPUSAM between MPUCTL0 password writes and
 * leave the MPU enabled.  The other ports keep the word in software; RP2350
 * also makes its .uninit region writable exactly while bit 9 is set.
 *
 * @param sam  New segment access word
 */
void tiku_mpu_arch_set_sam(uint16_t sam);

/**
 * @brief Read the MPU control word, in the layout of MSP430's MPUCTL0
 *
 * MSP430 parts with an MPU return MPUCTL0 itself; STM32N6 and MSP430 parts
 * without one return 0, and every other port returns a word it keeps in
 * software.  Bit 0, the enable bit, is set while the MPU is on.
 *
 * @return Current control word
 */
uint16_t tiku_mpu_arch_get_ctl(void);

/*---------------------------------------------------------------------------*/
/* INTERRUPT CONTROL                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Mask interrupts for a scoped NVM write.
 *
 * tiku_mpu_scoped_write() calls it before unlocking NVM.
 */
void tiku_mpu_arch_disable_irq(void);

/**
 * @brief Unmask interrupts after a scoped NVM write.
 *
 * tiku_mpu_scoped_write() calls it after relocking NVM.  Where it acts
 * (MSP430, RP2350, Ambiq) it enables interrupts unconditionally.
 */
void tiku_mpu_arch_enable_irq(void);

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS (CALLED BY THE KERNEL)                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Set up the port's memory protection
 *
 * MSP430 parts with an MPU write the segment boundaries (MPUSEGB1/2) and
 * enable the MPU; RP2350, Ambiq, Nordic and RA8P1 program their MPU regions,
 * ESP32-C61 locks a PMP entry over the first 4 KB, and STM32N6 does nothing.
 *
 * @note Call before tiku_mpu_arch_set_default_protection(): on MSP430 the
 *       permission bits apply to the segments it sets.
 */
void tiku_mpu_arch_init_segments(void);

/**
 * @brief Set default NVM protection on all segments
 *
 * Applies the port default, TIKU_MPU_DEFAULT_SAM: every segment read+execute,
 * except on MSP430 parts with HIFRAM, where segment 3 (HIFRAM) is also
 * writable.  The register encoding is the arch layer's.
 */
void tiku_mpu_arch_set_default_protection(void);

/**
 * @brief Arch hook for tiku_mpu_module_window_exec(); see kernel/memory.
 *
 * Ports with no RAM execution window implement this as a no-op.
 *
 * @param enable  1 = window RO + executable, 0 = window RW + execute-never.
 */
void tiku_mpu_arch_module_window_exec(int enable);

/**
 * @brief Set permissions on a single MPU segment
 *
 * Updates the permission bits for one segment without affecting others.
 * The seg and perm values correspond to the platform-independent
 * tiku_mpu_seg_t and tiku_mpu_perm_t enums (passed as uint8_t).
 *
 * @param seg    Segment index 0-2 (TIKU_MPU_SEG1..TIKU_MPU_SEG3)
 * @param perm   Permission flags (TIKU_MPU_READ/WRITE/EXEC or combinations)
 */
void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm);

/**
 * @brief Unlock NVM for writing on all segments
 *
 * Adds write permission to every segment.
 *
 * @return Previous protection state, opaque to the kernel
 * @note Pass the result to tiku_mpu_arch_lock_nvm() to restore the protection.
 */
uint16_t tiku_mpu_arch_unlock_nvm(void);

/**
 * @brief Restore NVM protection to a previously saved state
 *
 * @param saved_state  Value returned by a prior tiku_mpu_arch_unlock_nvm()
 */
void tiku_mpu_arch_lock_nvm(uint16_t saved_state);

/**
 * @brief Read the latched MPU violation flags
 *
 * MSP430 returns the MPUCTL1 flags its SYSNMI handler latched, bit n - 1 for
 * segment n; RP2350 and Ambiq the MMFSR bits their MemManage handler ORs in;
 * RA8P1 0x0002 after a MemManage access violation in its fault record.
 *
 * @return Violation flags in the port's encoding; 0 on the other ports
 */
uint16_t tiku_mpu_arch_get_violation_flags(void);

/**
 * @brief Clear all MPU violation flags
 *
 * Zeroes what tiku_mpu_arch_get_violation_flags() returns.  MSP430 also
 * clears MPUCTL1's flags and leaves the violation NMI on; RA8P1 clears the
 * fault record its flags come from.
 */
void tiku_mpu_arch_clear_violation_flags(void);

/**
 * @brief Make an MPU violation raise an NMI on MSP430
 *
 * MSP430 parts with an MPU set MPUSEGIE, so an access the MPU blocks raises
 * SYSNMI, whose handler latches the violated segment.  RP2350, Ambiq and
 * RA8P1 set bit 4 of their control word; the other ports do nothing.
 */
void tiku_mpu_arch_enable_violation_nmi(void);

#endif /* TIKU_MPU_HAL_H_ */
