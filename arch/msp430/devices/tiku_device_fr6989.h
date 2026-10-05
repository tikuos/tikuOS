/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_fr6989.h - MSP430FR6989 silicon-level constants.
 *
 * GPIO ports, crystal pin routing, memory sizes and peripheral availability;
 * PCB definitions belong in the board header.  HFXT is on PJ.6/PJ.7, and the
 * part carries an on-chip LCD_C driver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_FR6989_H_
#define TIKU_DEVICE_FR6989_H_

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** Part name, reported by the shell and /sys. */
#define TIKU_DEVICE_NAME            "MSP430FR6989"
#define TIKU_DEVICE_NVM_LABEL       "FRAM"   /**< NVM technology (UI label). */

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @name 1 for each GPIO port the part has, 0 for each it lacks
 * P10 (P10.0/P10.1 on the 100-pin package) has no macro, so
 * tiku_cpu_boot_msp430_pins_init_low() leaves it alone; on the LaunchPad
 * those pins are wired to the LCD glass.
 * @{
 */
#define TIKU_DEVICE_HAS_PORT1       1
#define TIKU_DEVICE_HAS_PORT2       1
#define TIKU_DEVICE_HAS_PORT3       1
#define TIKU_DEVICE_HAS_PORT4       1
#define TIKU_DEVICE_HAS_PORT5       1
#define TIKU_DEVICE_HAS_PORT6       1
#define TIKU_DEVICE_HAS_PORT7       1
#define TIKU_DEVICE_HAS_PORT8       1
#define TIKU_DEVICE_HAS_PORT9       1
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

#define TIKU_DEVICE_FRAM_SIZE       (128 * 1024UL)  /**< 128 KB FRAM */
#define TIKU_DEVICE_RAM_SIZE        (2 * 1024UL)    /**< 2 KB SRAM */
#define TIKU_DEVICE_RAM_START       0x1C00U         /**< First byte of SRAM */

/*---------------------------------------------------------------------------*/
/* FRAM ADDRESS RANGE                                                        */
/*---------------------------------------------------------------------------*/

/*
 * Lower-FRAM 16-bit window (0x4400-0xFF7F is code/data; vectors at 0xFF80).
 * The other ~80 KB of FRAM lives at 0x10000+ (HIFRAM) and holds data through
 * the .upper.{data,bss,rodata} linker sections (the TIKU_HIFRAM* macros in
 * <kernel/memory/tiku_mem.h>).  Code is placed there only with
 * MEMORY_MODEL=large.
 */
#define TIKU_DEVICE_FRAM_START      0x4400U  /**< First byte of main FRAM */
#define TIKU_DEVICE_FRAM_END        0xFFFFU  /**< Last byte of lower window */

#define TIKU_DEVICE_HAS_HIFRAM      1          /**< FRAM above 64 KB exists */
#define TIKU_DEVICE_HIFRAM_START    0x10000UL  /**< First byte of HIFRAM */
#define TIKU_DEVICE_HIFRAM_END      0x23FF6UL  /**< Last byte of HIFRAM */

/*---------------------------------------------------------------------------*/
/* MPU (MEMORY PROTECTION UNIT)                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_MPU         1   /**< FR6989 has hardware MPU */

/*
 * MPU segments on this part:
 *
 *   Segment 1: 0x4400 - 0x7FFF   (~15 KB, code + persistent)   R+X
 *   Segment 2: 0x8000 - 0xFFFF   (32 KB,  code + vectors)      R+X
 *   Segment 3: 0x10000 - 0x23FFF (80 KB,  HIFRAM data)         R+W+X
 *
 * Segment 3 holds only HIFRAM, so the write permission TIKU_MPU_DEFAULT_SAM
 * gives it covers .upper.bss and .upper.data (MEMORY_MODEL=large) and no
 * lower-FRAM code.  tiku_mpu_arch_init_segments() writes the boundaries,
 * shifted right by 4, to MPUSEGB1 and MPUSEGB2.
 */
/** @name MPU segment boundaries, written by tiku_mpu_arch_init_segments()
 * @{ */
#define TIKU_DEVICE_MPU_SEG2_START  0x8000U
#define TIKU_DEVICE_MPU_SEG3_START  0x10000UL
/** @} */

/*---------------------------------------------------------------------------*/
/* eUSCI PERIPHERAL AVAILABILITY                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_EUSCIA0     1   /**< eUSCI_A0 present (UART/SPI) */
#define TIKU_DEVICE_HAS_EUSCIA1     1   /**< eUSCI_A1 present (UART/SPI) */
#define TIKU_DEVICE_HAS_EUSCIB0     1   /**< eUSCI_B0 present (I2C/SPI) */
#define TIKU_DEVICE_HAS_EUSCIB1     1   /**< eUSCI_B1 present (I2C/SPI) */

/*---------------------------------------------------------------------------*/
/* ADC PERIPHERAL                                                            */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_ADC12B      1   /**< ADC12_B present (12-bit SAR) */
#define TIKU_DEVICE_ADC_CHANNELS    16  /**< External channels A0-A15 */

/**
 * @brief External pin of each ADC12_B channel, encoded (port << 4) | bit.
 *
 * A4-A7 run down P8.7-P8.4 and A8-A15 are P9.0-P9.7.
 */
#define TIKU_DEVICE_ADC_PIN_MAP                                     \
    { 0x10, 0x11, 0x12, 0x13,   /* A0-A3   P1.0-P1.3 */             \
      0x87, 0x86, 0x85, 0x84,   /* A4-A7   P8.7-P8.4 (descending) */\
      0x90, 0x91, 0x92, 0x93,   /* A8-A11  P9.0-P9.3 */             \
      0x94, 0x95, 0x96, 0x97 }  /* A12-A15 P9.4-P9.7 */

/*---------------------------------------------------------------------------*/
/* LCD CONTROLLER                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief 1: the part has the LCD_C segment driver.
 *
 * LCD_C drives up to 320 segments and has a built-in charge pump.  The
 * LaunchPad wires it to an FH-1138P glass (see the board header).
 */
#define TIKU_DEVICE_HAS_LCD_C       1

/*---------------------------------------------------------------------------*/
/* FRAM REGION BUDGET                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bytes of the config region, which holds the init table.
 *
 * kernel/memory/tiku_nvm_map.c declares the region at this size and the
 * linker places it; kernel/init/tiku_init.c asserts the init table fits.
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      2048U   /* Init table */

/** @name Loadable app slot geometry; no code in the tree allocates slots
 * @{ */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    8192U   /**< 8 KB per app slot */
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   8       /**< 8 slots */
/** @} */

#endif /* TIKU_DEVICE_FR6989_H_ */
