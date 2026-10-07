/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - MSP430 MPU register access.
 *
 * Every MPUCTL0 and MPUSAM access is in this file.  Each MPUCTL0 write
 * carries the password MPUPW in its upper byte.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mpu_arch.h"
#include "tiku_device_select.h"
#include <hal/tiku_compiler.h>
#include <msp430.h>

#if TIKU_DEVICE_HAS_MPU

/*---------------------------------------------------------------------------*/
/* MPU SEGMENT BOUNDARY SETUP                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Set up the three MPU segment boundaries from device macros.
 *
 * Writes MPUSEGB1 and MPUSEGB2 (boundary addresses right-shifted by 4) to
 * divide the FRAM address space into three protection segments, and
 * enables the MPU.  SAM permissions apply to the segments set here.
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
 * MPUCTL0 takes the password before MPUSAM changes and again with MPUENA
 * after, so callers never handle the password or the enable bit.
 * MPUSEGIE (violation NMI enable) is carried across the write, so a
 * permission change keeps the violation NMI armed.
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
 * The functions below take portable segment numbers and permission flags
 * and do the SAM bit arithmetic for the kernel.
 */

/**
 * @brief Set default NVM protection.
 *
 * Without HIFRAM every segment is R+X, since all three hold persistent data,
 * vectors and code.  With HIFRAM, segment 3 holds kernel mutable state and
 * is R+W+X: a store there without W is dropped and boot fails.
 */
void tiku_mpu_arch_set_default_protection(void)
{
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

/**
 * @brief Set permissions on a single MPU segment.
 *
 * Each segment owns a nybble of the access-mask register, whose low three bits
 * are read, write and execute.  Clears the old bits and sets the new ones,
 * leaving other segments untouched.
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
 * MPUCTL1 as the SYSNMI ISR latched it before reading SYSSNIV, which
 * clears the hardware flags.  The ISR ORs into it until a clear.
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
 * With MPUSEGIE set, an MPU violation raises a System NMI.  The ISR ORs
 * MPUCTL1 into the latch before reading SYSSNIV, because that read clears
 * the hardware violation flags; tiku_mpu_arch_get_violation_flags()
 * returns the latch.
 */
TIKU_ISR(SYSNMI_VECTOR, tiku_mpu_sysnmi_isr)
{
    latched_violation_flags |= MPUCTL1;
    (void)SYSSNIV;  /* Acknowledge NMI — clears hardware flags */
}

#else /* !TIKU_DEVICE_HAS_MPU */

/*---------------------------------------------------------------------------*/
/* NO-OP STUBS FOR DEVICES WITHOUT MPU                                       */
/*---------------------------------------------------------------------------*/

/* Without an MPU, FR2433 write windows use SYSCFG0.PFWP. Other devices
 * leave protection unchanged; interrupt wrappers still mask and unmask. */
void     tiku_mpu_arch_init_segments(void)           { }
uint16_t tiku_mpu_arch_get_sam(void)                 { return 0; }
void     tiku_mpu_arch_set_sam(uint16_t sam)          { (void)sam; }
uint16_t tiku_mpu_arch_get_ctl(void)                 { return 0; }
void     tiku_mpu_arch_disable_irq(void)             { __disable_interrupt(); }
void     tiku_mpu_arch_enable_irq(void)              { __enable_interrupt(); }
void     tiku_mpu_arch_set_default_protection(void)  { }
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)
                                                      { (void)seg; (void)perm; }
uint16_t tiku_mpu_arch_unlock_nvm(void)
{
#if defined(TIKU_DEVICE_MSP430FR2433)
    uint16_t saved = SYSCFG0 & 0x00ffu;
    SYSCFG0 = FRWPPW | (saved & ~PFWP);
    return saved;
#else
    return 0;
#endif
}
void tiku_mpu_arch_lock_nvm(uint16_t saved)
{
#if defined(TIKU_DEVICE_MSP430FR2433)
    SYSCFG0 = FRWPPW | (saved & 0x00ffu);
#else
    (void)saved;
#endif
}
uint16_t tiku_mpu_arch_get_violation_flags(void)     { return 0; }
void     tiku_mpu_arch_clear_violation_flags(void)   { }
void     tiku_mpu_arch_enable_violation_nmi(void)    { }

#endif /* TIKU_DEVICE_HAS_MPU */

/* Bottom of the stack for stack painting: the linker places all static
 * SRAM below _end and the stack descends from the top of the selected RAM
 * region, so painting starts above the statics and fixed tier buffers. */
extern char _end;
uint32_t tiku_stack_arch_bottom(void)
{
    return (uint32_t)(uintptr_t)&_end;
}

/**
 * @brief No-op: this port has no RAM execution window.
 *
 * A loaded module runs in place from FRAM, so there is nothing to switch.
 */
void tiku_mpu_arch_module_window_exec(int enable)
{
    (void)enable;
}
