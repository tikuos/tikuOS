/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
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

uint16_t tiku_mpu_arch_get_ctl(void)
{
    return MPUCTL0;
}

void tiku_mpu_arch_disable_irq(void)
{
    __disable_interrupt();
}

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

void tiku_mpu_arch_set_default_protection(void)
{
    /* Without HIFRAM all three segments hold persistent data, vectors and
     * code, and none is writable.  With HIFRAM, segment 3 holds kernel
     * mutable state and is writable: a store there without W is dropped and
     * boot fails. */
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)
{
    uint16_t shift = (uint16_t)seg * 4U;
    uint16_t mask  = (uint16_t)0x07 << shift;
    uint16_t sam   = tiku_mpu_arch_get_sam();

    sam = (sam & ~mask) | (((uint16_t)perm & 0x07) << shift);
    tiku_mpu_arch_set_sam(sam);
}

uint16_t tiku_mpu_arch_unlock_nvm(void)
{
    uint16_t saved = tiku_mpu_arch_get_sam();

    tiku_mpu_arch_set_sam(saved | 0x0222);

    return saved;
}

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

uint16_t tiku_mpu_arch_get_violation_flags(void)
{
    return latched_violation_flags;
}

void tiku_mpu_arch_clear_violation_flags(void)
{
    latched_violation_flags = 0;
    MPUCTL0 = MPUPW;                        /* Unlock config */
    MPUCTL1 &= ~(MPUSEG1IFG | MPUSEG2IFG | MPUSEG3IFG);
    MPUCTL0 = MPUPW | MPUENA | MPUSEGIE;   /* Re-enable MPU + NMI */
}

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

/* A device without an MPU leaves protection unchanged; the interrupt
 * wrappers still mask and unmask. */
void     tiku_mpu_arch_init_segments(void)           { }
uint16_t tiku_mpu_arch_get_sam(void)                 { return 0; }
void     tiku_mpu_arch_set_sam(uint16_t sam)          { (void)sam; }
uint16_t tiku_mpu_arch_get_ctl(void)                 { return 0; }
void     tiku_mpu_arch_disable_irq(void)             { __disable_interrupt(); }
void     tiku_mpu_arch_enable_irq(void)              { __enable_interrupt(); }
void     tiku_mpu_arch_set_default_protection(void)  { }
void     tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)
                                                      { (void)seg; (void)perm; }
uint16_t tiku_mpu_arch_unlock_nvm(void)            { return 0; }
void     tiku_mpu_arch_lock_nvm(uint16_t saved)     { (void)saved; }
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
