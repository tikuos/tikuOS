/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - MSP430 MPU architecture implementation
 *
 * Implements the arch-level MPU register access for MSP430FR series.
 * All accesses to MPUCTL0 and MPUSAM go through this file, keeping
 * the password handling and hardware details in one place.
 *
 * The MSP430 MPU registers are password-protected: every write to
 * MPUCTL0 must include MPUPW (0xA500) in the upper byte. This
 * prevents wild pointer writes from accidentally modifying memory
 * protection configuration.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "tiku_device_select.h"
#include "tiku_compiler.h"
#include <msp430.h>

/*---------------------------------------------------------------------------*/
/* MPU SEGMENT BOUNDARY SETUP                                                */
/*---------------------------------------------------------------------------*/

/*
 * The MPU needs valid segment boundaries before SAM permissions
 * have any effect. MPUSEGB1 and MPUSEGB2 divide the FRAM address
 * space into three segments. The register values are the actual
 * boundary addresses right-shifted by 4.
 */
/**
 * @brief Set up the three MPU segment boundaries from device macros.
 *
 * Writes MPUSEGB1 and MPUSEGB2 (boundary addresses right-shifted by 4)
 * to divide the FRAM address space into three protection segments.
 * Must be called before SAM permissions have any effect.
 */
void tiku_mpu_arch_init_segments(void)
{
    MPUCTL0  = MPUPW;                                  /* Unlock config */
    MPUSEGB1 = TIKU_DEVICE_MPU_SEG2_START >> 4;
    MPUSEGB2 = TIKU_DEVICE_MPU_SEG3_START >> 4;
    MPUCTL0  = MPUPW | MPUENA;                         /* Re-enable MPU */
}

/*---------------------------------------------------------------------------*/
/* MPU REGISTER ACCESS                                                       */
/*---------------------------------------------------------------------------*/

/** @brief Read the current MPU Segment Access Management register. */
uint16_t tiku_mpu_arch_get_sam(void)
{
    return MPUSAM;
}

/*
 * Why unlock-write-enable in every set_sam call:
 *   The MPU config registers are locked by default. To modify MPUSAM
 *   we must write the password to MPUCTL0 first, then change MPUSAM,
 *   then write password | enable to MPUCTL0 to re-activate. Bundling
 *   this into set_sam means the kernel never needs to know about the
 *   password or the enable bit.
 *
 *   MPUSEGIE (violation NMI enable) is preserved across calls so that
 *   enabling violation detection is not accidentally undone by a later
 *   permission change.
 */
void tiku_mpu_arch_set_sam(uint16_t sam)
{
    uint16_t flags = MPUCTL0 & MPUSEGIE;  /* Preserve MPUSEGIE */
    MPUCTL0 = MPUPW;                       /* Unlock config */
    MPUSAM  = sam;                          /* Set permissions */
    MPUCTL0 = MPUPW | MPUENA | flags;     /* Re-enable + preserved flags */
}

/** @brief Read the raw MPUCTL0 register value. */
uint16_t tiku_mpu_arch_get_ctl(void)
{
    return MPUCTL0;
}

/** @brief Disable global interrupts (wrapper around __disable_interrupt). */
void tiku_mpu_arch_disable_irq(void)
{
    __disable_interrupt();
}

/** @brief Enable global interrupts (wrapper around __enable_interrupt). */
void tiku_mpu_arch_enable_irq(void)
{
    __enable_interrupt();
}

/*---------------------------------------------------------------------------*/
/* HIGHER-LEVEL ARCH FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/*
 * These functions encapsulate MSP430-specific register-level details
 * (SAM bit layout, write-bit positions) so the kernel never needs to
 * know the register format. The kernel calls these instead of
 * manipulating SAM values directly.
 */

/**
 * @brief Set default NVM protection: R+X (no write) on all 3 segments
 *
 * SAM value 0x0555: each segment's nybble is 0x5 = R | X (no W).
 */
void tiku_mpu_arch_set_default_protection(void)
{
    tiku_mpu_arch_set_sam(0x0555);
}

/**
 * @brief Set permissions on a single MPU segment
 *
 * Each segment occupies a 4-bit nybble in the SAM register. The lower
 * 3 bits are R/W/X permissions; the 4th bit is reserved. This function
 * clears the old permission bits and sets the new ones, leaving all
 * other segments untouched.
 *
 * @param seg    Segment number (0-2)
 * @param perm   Permission flags (TIKU_MPU_READ/WRITE/EXEC or combinations)
 */
void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)
{
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t mask  = (uint16_t)0x07 << shift;
    uint16_t sam   = tiku_mpu_arch_get_sam();

    sam = (sam & ~mask) | (((uint16_t)perm & 0x07) << shift);
    tiku_mpu_arch_set_sam(sam);
}

/**
 * @brief Unlock NVM for writing on all segments
 *
 * ORs the write bit (bit 1) into each segment's nybble:
 *   0x0222 = write bit set for all 3 segments.
 *
 * @return Previous SAM value (opaque to the kernel, used by lock_nvm)
 */
uint16_t tiku_mpu_arch_unlock_nvm(void)
{
    uint16_t saved = tiku_mpu_arch_get_sam();

    tiku_mpu_arch_set_sam(saved | 0x0222);

    return saved;
}

/**
 * @brief Restore NVM protection to a previously saved state
 *
 * @param saved_state  Value returned by a prior tiku_mpu_arch_unlock_nvm()
 */
void tiku_mpu_arch_lock_nvm(uint16_t saved_state)
{
    tiku_mpu_arch_set_sam(saved_state);
}

/*---------------------------------------------------------------------------*/
/* MPU VIOLATION FLAGS                                                       */
/*---------------------------------------------------------------------------*/

/*
 * Software-latched violation flags. Reading SYSSNIV in the NMI ISR
 * clears the corresponding MPUCTL1 flag as a hardware side effect.
 * To let callers inspect which segment was violated, the ISR saves
 * MPUCTL1 here before acknowledging the interrupt.
 */
static volatile uint16_t latched_violation_flags;

/** @brief Return the software-latched MPU violation flags. */
uint16_t tiku_mpu_arch_get_violation_flags(void)
{
    return latched_violation_flags;
}

/** @brief Clear both the software latch and hardware MPUCTL1 flags. */
void tiku_mpu_arch_clear_violation_flags(void)
{
    latched_violation_flags = 0;
    MPUCTL0 = MPUPW;                        /* Unlock config */
    MPUCTL1 &= ~(MPUSEG1IFG | MPUSEG2IFG | MPUSEG3IFG);
    MPUCTL0 = MPUPW | MPUENA | MPUSEGIE;   /* Re-enable MPU + NMI */
}

/** @brief Enable the MPU violation NMI (MPUSEGIE bit). */
void tiku_mpu_arch_enable_violation_nmi(void)
{
    MPUCTL0 = MPUPW | MPUENA | MPUSEGIE;
}

/*---------------------------------------------------------------------------*/
/* SYSNMI ISR — MPU VIOLATION HANDLER                                        */
/*---------------------------------------------------------------------------*/

/*
 * When MPUSEGIE is set, an MPU violation triggers a System NMI instead
 * of a PUC (reset). We latch MPUCTL1 before reading SYSSNIV because
 * the read clears the hardware violation flags. The latched value
 * can then be inspected by tiku_mpu_get_violation_flags().
 */
TIKU_ISR(SYSNMI_VECTOR, tiku_mpu_sysnmi_isr)
{
    latched_violation_flags |= MPUCTL1;
    (void)SYSSNIV;  /* Acknowledge NMI — clears hardware flags */
}
