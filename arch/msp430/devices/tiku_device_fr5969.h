/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_fr5969.h - MSP430FR5969 silicon-level constants
 *
 * This header defines the hardware capabilities of the MSP430FR5969
 * microcontroller: available GPIO ports, crystal pin routing, memory
 * sizes, and peripheral availability. Board-level (PCB) definitions
 * such as LED and button pin assignments belong in the board header.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_FR5969_H_
#define TIKU_DEVICE_FR5969_H_

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_NAME            "MSP430FR5969"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_PORT1       1
#define TIKU_DEVICE_HAS_PORT2       1
#define TIKU_DEVICE_HAS_PORT3       1
#define TIKU_DEVICE_HAS_PORT4       1
#define TIKU_DEVICE_HAS_PORT5       0
#define TIKU_DEVICE_HAS_PORT6       0
#define TIKU_DEVICE_HAS_PORT7       0
#define TIKU_DEVICE_HAS_PORT8       0
#define TIKU_DEVICE_HAS_PORT9       0
#define TIKU_DEVICE_HAS_PORTJ       1

/*---------------------------------------------------------------------------*/
/* CRYSTAL PIN ROUTING                                                       */
/*---------------------------------------------------------------------------*/

/** LFXT (32.768 kHz) crystal pins: PJ.4 = LFXIN, PJ.5 = LFXOUT */
#define TIKU_DEVICE_LFXT_PSEL_REG       PJSEL0
#define TIKU_DEVICE_LFXT_PSEL_BITS      (BIT4 | BIT5)
#define TIKU_DEVICE_LFXT_PSEL1_REG      PJSEL1
#define TIKU_DEVICE_LFXT_PSEL1_BITS     (BIT4 | BIT5)

/** HFXT crystal pins: PJ.2 = HFXIN, PJ.3 = HFXOUT */
#define TIKU_DEVICE_HFXT_PSEL_REG       PJSEL0
#define TIKU_DEVICE_HFXT_PSEL_BITS      (BIT2 | BIT3)
#define TIKU_DEVICE_HFXT_PSEL1_REG      PJSEL1
#define TIKU_DEVICE_HFXT_PSEL1_BITS     (BIT2 | BIT3)

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_LFXT        1
#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_CS_HAS_KEY      1
#define TIKU_DEVICE_MAX_STABLE_MHZ  8

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_FRAM_SIZE       (64 * 1024UL)   /* 64 KB FRAM */
#define TIKU_DEVICE_RAM_SIZE        (2 * 1024UL)    /* 2 KB SRAM */
#define TIKU_DEVICE_RAM_START       0x1C00U         /* First byte of SRAM */

/*---------------------------------------------------------------------------*/
/* FRAM ADDRESS RANGE                                                        */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_FRAM_START      0x4400U  /* First byte of main FRAM */
#define TIKU_DEVICE_FRAM_END        0xFFFFU  /* Last byte of main FRAM */

/*---------------------------------------------------------------------------*/
/* MPU (MEMORY PROTECTION UNIT)                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_MPU         1   /**< FR5969 has hardware MPU */

/* MPU SEGMENT BOUNDARIES */

/*
 * Default 3-way partition of main FRAM for MPU protection:
 *   Segment 1: 0x4400 – 0x7FFF  (~15 KB, code)
 *   Segment 2: 0x8000 – 0xBFFF  (16 KB, code/data)
 *   Segment 3: 0xC000 – 0xFFFF  (16 KB, data + vectors)
 *
 * These are actual addresses. The arch code shifts right by 4 before
 * writing to the MPUSEGB registers.
 */
#define TIKU_DEVICE_MPU_SEG2_START  0x8000U
#define TIKU_DEVICE_MPU_SEG3_START  0xC000U

/*---------------------------------------------------------------------------*/
/* eUSCI PERIPHERAL AVAILABILITY                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_EUSCIA0     1   /**< eUSCI_A0 present (UART) */
#define TIKU_DEVICE_HAS_EUSCIA1     1   /**< eUSCI_A1 present (SPI) */
#define TIKU_DEVICE_HAS_EUSCIB0     1   /**< eUSCI_B0 present (I2C) */
#define TIKU_DEVICE_HAS_EUSCIB1     1   /**< eUSCI_B1 present */

/*---------------------------------------------------------------------------*/
/* ADC PERIPHERAL                                                            */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_ADC12B      1   /**< ADC12_B present (12-bit SAR) */
#define TIKU_DEVICE_ADC_CHANNELS    16  /**< External channels A0-A15 */

/**
 * External analog input pin for each ADC12_B channel, indexed by
 * channel number and encoded as (port << 4) | bit — so 0x23 is P2.3.
 * The analog function is selected by setting BOTH PxSEL0 and PxSEL1.
 *
 * Source: SLAS704 (MSP430FR5969 family) pinout, A0-A15.
 */
#define TIKU_DEVICE_ADC_PIN_MAP                                     \
    { 0x10, 0x11, 0x12, 0x13,   /* A0-A3   P1.0-P1.3 */             \
      0x14, 0x15, 0x23, 0x24,   /* A4-A7   P1.4,P1.5,P2.3,P2.4 */   \
      0x40, 0x41, 0x42, 0x43,   /* A8-A11  P4.0-P4.3 */             \
      0x30, 0x31, 0x32, 0x33 }  /* A12-A15 P3.0-P3.3 */

/*---------------------------------------------------------------------------*/
/* FRAM REGION BUDGET                                                        */
/*---------------------------------------------------------------------------*/

/**
 * Per-device sizing for FRAM-backed regions.  The kernel/memory/tiku_fram_map
 * module reads these to declare storage arrays; the linker places them.
 * Adjust sizes per device — the rest of the system adapts automatically.
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      1024U   /* Init table + credentials */

/* Future: loadable app slots (reserved IDs, not allocated until enabled) */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    4096U   /* 4 KB per app slot */
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   3       /* 3 slots on 64 KB part */

#endif /* TIKU_DEVICE_FR5969_H_ */
