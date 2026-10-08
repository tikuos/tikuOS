/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_fr5994.h - MSP430FR5994 silicon-level constants
 *
 * This header defines the hardware capabilities of the MSP430FR5994
 * microcontroller: available GPIO ports, crystal pin routing, memory
 * sizes, and peripheral availability.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_FR5994_H_
#define TIKU_DEVICE_FR5994_H_

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** Part name, reported by the shell and /sys. */
#define TIKU_DEVICE_NAME            "MSP430FR5994"
#define TIKU_DEVICE_NVM_LABEL       "FRAM"   /**< NVM technology (UI label). */

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/** @name 1 for each GPIO port the part has, 0 for each it lacks
 * @{ */
#define TIKU_DEVICE_HAS_PORT1       1
#define TIKU_DEVICE_HAS_PORT2       1
#define TIKU_DEVICE_HAS_PORT3       1
#define TIKU_DEVICE_HAS_PORT4       1
#define TIKU_DEVICE_HAS_PORT5       1
#define TIKU_DEVICE_HAS_PORT6       1
#define TIKU_DEVICE_HAS_PORT7       1
#define TIKU_DEVICE_HAS_PORT8       1
#define TIKU_DEVICE_HAS_PORT9       0
#define TIKU_DEVICE_HAS_PORTJ       1
/** @} */

/*---------------------------------------------------------------------------*/
/* CRYSTAL PIN ROUTING                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @name LFXT (32.768 kHz) crystal pins: PJ.4 = LFXIN, PJ.5 = LFXOUT
 * Crystal start-up sets PSEL_BITS in PSEL_REG and clears PSEL1_BITS in
 * PSEL1_REG.
 * @{
 */
#define TIKU_DEVICE_LFXT_PSEL_REG       PJSEL0
#define TIKU_DEVICE_LFXT_PSEL_BITS      (BIT4 | BIT5)
#define TIKU_DEVICE_LFXT_PSEL1_REG      PJSEL1
#define TIKU_DEVICE_LFXT_PSEL1_BITS     (BIT4 | BIT5)
/** @} */

/**
 * @name HFXT crystal pins: PJ.6 = HFXIN, PJ.7 = HFXOUT
 * Crystal start-up sets PSEL_BITS in PSEL_REG and clears PSEL1_BITS in
 * PSEL1_REG.
 * @{
 */
#define TIKU_DEVICE_HFXT_PSEL_REG       PJSEL0
#define TIKU_DEVICE_HFXT_PSEL_BITS      (BIT6 | BIT7)
#define TIKU_DEVICE_HFXT_PSEL1_REG      PJSEL1
#define TIKU_DEVICE_HFXT_PSEL1_BITS     (BIT6 | BIT7)
/** @} */

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_LFXT        1   /**< LFXT crystal oscillator present */
#define TIKU_DEVICE_HAS_HFXT        1   /**< HFXT crystal oscillator present */
#define TIKU_DEVICE_CS_HAS_KEY      1   /**< CS registers unlock with CSKEY */
#define TIKU_DEVICE_MAX_STABLE_MHZ  16  /**< Maximum stable MCLK, in MHz */

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_FRAM_SIZE       (256 * 1024UL)  /**< 256 KB FRAM */

/*
 * The FR5994 has 8 KB of SRAM.  The toolchain's stock linker script splits it
 * into RAM (4 KB), LEARAM (~3.7 KB) and LEASTACK (312 B).  The Makefile links
 * msp430fr5994_8k_ram.ld, which merges the three into one 8 KB RAM region,
 * and defines TIKU_FR5994_LEA_DISABLED; it refuses LEA_ENABLE other than 0.
 * A build outside the Makefile keeps the stock 4 KB RAM region.
 */
#ifdef TIKU_FR5994_LEA_DISABLED
#define TIKU_DEVICE_RAM_SIZE        (8 * 1024UL)    /**< 8 KB merged RAM */
#else
#define TIKU_DEVICE_RAM_SIZE        (4 * 1024UL)    /**< 4 KB stock RAM */
#endif
#define TIKU_DEVICE_RAM_START       0x1C00U         /**< First byte of SRAM */

/*---------------------------------------------------------------------------*/
/* FRAM REGION SIZING (used by kernel/memory/tiku_nvm_map)                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bytes of the config region, which holds the init table.
 *
 * kernel/memory/tiku_nvm_map.c declares the region at this size and the
 * linker places it; services/init/tiku_init.c asserts the init table fits.
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      1024U   /* Init table */

/** @name Loadable app slot geometry; no code in the tree allocates slots
 * @{ */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    8192U   /**< 8 KB per app slot */
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   8       /**< 8 slots */
/** @} */

/*---------------------------------------------------------------------------*/
/* FRAM ADDRESS RANGE                                                        */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_FRAM_START      0x4000U  /**< First byte of main FRAM */
#define TIKU_DEVICE_FRAM_END        0xFFFFU  /**< Last byte of lower 64 KB */

/*
 * The other ~208 KB of FRAM lives at 0x10000+ (HIFRAM).  Data reaches it
 * through the TIKU_HIFRAM* macros in <kernel/memory/tiku_mem.h>; code is
 * placed there only with MEMORY_MODEL=large.
 */
#define TIKU_DEVICE_HAS_HIFRAM      1          /**< FRAM above 64 KB exists */
#define TIKU_DEVICE_HIFRAM_START    0x10000UL  /**< First byte of HIFRAM */
#define TIKU_DEVICE_HIFRAM_END      0x43FF6UL  /**< Last byte of HIFRAM */

/*---------------------------------------------------------------------------*/
/* MPU (MEMORY PROTECTION UNIT)                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_MPU         1   /**< FR5994 has hardware MPU */

/*
 * Segments 1 (0x4000-0x7FFF) and 2 (0x8000-0xFFFF) hold lower FRAM: code,
 * vectors and persistent data, at R+X.  Segment 3 starts at HIFRAM and is
 * R+W+X (TIKU_MPU_DEFAULT_SAM), so .upper.bss and .upper.data need no unlock
 * under MEMORY_MODEL=large.  A SEG3_START below 0x10000 would give that
 * write permission to lower-FRAM code too.
 */
/** @name MPU segment boundaries, written by tiku_mpu_arch_init_segments()
 * @{ */
#define TIKU_DEVICE_MPU_SEG2_START  0x8000U
#define TIKU_DEVICE_MPU_SEG3_START  0x10000UL
/** @} */

/*---------------------------------------------------------------------------*/
/* eUSCI PERIPHERAL AVAILABILITY                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_EUSCIA0     1   /**< eUSCI_A0 present (UART) */
#define TIKU_DEVICE_HAS_EUSCIA1     1   /**< eUSCI_A1 present */
#define TIKU_DEVICE_HAS_EUSCIA2     1   /**< eUSCI_A2 present */
#define TIKU_DEVICE_HAS_EUSCIA3     1   /**< eUSCI_A3 present */
#define TIKU_DEVICE_HAS_EUSCIB0     1   /**< eUSCI_B0 present (I2C) */
#define TIKU_DEVICE_HAS_EUSCIB1     1   /**< eUSCI_B1 present */
#define TIKU_DEVICE_HAS_EUSCIB2     1   /**< eUSCI_B2 present */
#define TIKU_DEVICE_HAS_EUSCIB3     1   /**< eUSCI_B3 present */

/*---------------------------------------------------------------------------*/
/* ADC PERIPHERAL                                                            */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_ADC12B      1   /**< ADC12_B present (12-bit SAR) */
#define TIKU_DEVICE_ADC_CHANNELS    32  /**< ADC12_B input channels A0-A31 */

/**
 * @brief External pin of each ADC12_B channel, encoded (port << 4) | bit.
 *
 * A16-A19 are P7.4-P7.7; channels above A19 have no external pin.  Source:
 * SLASE54D pinout.
 */
#define TIKU_DEVICE_ADC_PIN_MAP                                     \
    { 0x10, 0x11, 0x12, 0x13,   /* A0-A3   P1.0-P1.3 */             \
      0x14, 0x15, 0x23, 0x24,   /* A4-A7   P1.4,P1.5,P2.3,P2.4 */   \
      0x40, 0x41, 0x42, 0x43,   /* A8-A11  P4.0-P4.3 */             \
      0x30, 0x31, 0x32, 0x33,   /* A12-A15 P3.0-P3.3 */             \
      0x74, 0x75, 0x76, 0x77 }  /* A16-A19 P7.4-P7.7 */

#endif /* TIKU_DEVICE_FR5994_H_ */
