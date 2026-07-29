/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_fr6989.h - MSP430FR6989 silicon-level constants.
 *
 * GPIO ports, crystal pin routing, memory sizes and peripheral availability; PCB
 * definitions belong in the board header.  Note HFXT is on PJ.6/PJ.7, matching
 * FR5994 rather than FR5969, and the part carries an on-chip LCD_C driver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_FR6989_H_
#define TIKU_DEVICE_FR6989_H_

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_NAME            "MSP430FR6989"
#define TIKU_DEVICE_NVM_LABEL       "FRAM"   /**< NVM technology (UI label). */

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/*
 * The 100-pin FR6989 also has P10.0/P10.1, but those pins are wired to
 * the LCD glass on the LaunchPad and are managed by LCD_C when enabled.
 * The kernel does not actively manage P10, so it is not declared here —
 * the freq_boot init only iterates HAS_PORT1..HAS_PORT9.
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

/*---------------------------------------------------------------------------*/
/* CRYSTAL PIN ROUTING                                                       */
/*---------------------------------------------------------------------------*/

/** LFXT (32.768 kHz) crystal pins: PJ.4 = LFXIN, PJ.5 = LFXOUT */
#define TIKU_DEVICE_LFXT_PSEL_REG       PJSEL0
#define TIKU_DEVICE_LFXT_PSEL_BITS      (BIT4 | BIT5)
#define TIKU_DEVICE_LFXT_PSEL1_REG      PJSEL1
#define TIKU_DEVICE_LFXT_PSEL1_BITS     (BIT4 | BIT5)

/** HFXT crystal pins: PJ.6 = HFXIN, PJ.7 = HFXOUT */
#define TIKU_DEVICE_HFXT_PSEL_REG       PJSEL0
#define TIKU_DEVICE_HFXT_PSEL_BITS      (BIT6 | BIT7)
#define TIKU_DEVICE_HFXT_PSEL1_REG      PJSEL1
#define TIKU_DEVICE_HFXT_PSEL1_BITS     (BIT6 | BIT7)

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_LFXT        1
#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_CS_HAS_KEY      1
#define TIKU_DEVICE_MAX_STABLE_MHZ  16

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_FRAM_SIZE       (128 * 1024UL)  /* 128 KB FRAM */
#define TIKU_DEVICE_RAM_SIZE        (2 * 1024UL)    /* 2 KB SRAM */
#define TIKU_DEVICE_RAM_START       0x1C00U         /* First byte of SRAM */

/*---------------------------------------------------------------------------*/
/* FRAM ADDRESS RANGE                                                        */
/*---------------------------------------------------------------------------*/

/*
 * Lower-FRAM 16-bit window (0x4400-0xFF7F is code/data; vectors at 0xFF80).
 * The remaining ~80 KB of FRAM lives at 0x10000+ (HIFRAM) and is reachable
 * for *data* via the .upper.{data,bss,rodata} linker sections (use the
 * TIKU_HIFRAM* macros in <kernel/memory/tiku_mem.h>). Code only goes up
 * there with the large memory model — see Makefile MEMORY_MODEL=large.
 */
#define TIKU_DEVICE_FRAM_START      0x4400U  /* First byte of main FRAM */
#define TIKU_DEVICE_FRAM_END        0xFFFFU  /* Last byte of lower window */

#define TIKU_DEVICE_HAS_HIFRAM      1
#define TIKU_DEVICE_HIFRAM_START    0x10000UL  /* First byte of HIFRAM */
#define TIKU_DEVICE_HIFRAM_END      0x23FF6UL  /* Last byte of HIFRAM (~80 KB) */

/*---------------------------------------------------------------------------*/
/* MPU (MEMORY PROTECTION UNIT)                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_MPU         1   /**< FR6989 has hardware MPU */

/*
 * MPU segment layout for FR6989: segment 3 is reserved for HIFRAM so
 * that under MEMORY_MODEL=large the .upper.bss / .upper.data placed
 * at 0x10000+ can be granted R+W+X without affecting protection of
 * lower-FRAM code, vectors, or persistent data.
 *
 *   Segment 1: 0x4400 - 0x7FFF   (~15 KB, code + persistent)
 *   Segment 2: 0x8000 - 0xFFFF   (32 KB,  code + vectors)
 *   Segment 3: 0x10000 - 0x23FFF (80 KB,  HIFRAM data — large mode only)
 *
 * On parts without HIFRAM (FR5969, FR2433) segment 3 traditionally
 * covered the high lower-FRAM region; here we push it past the lower
 * window so HIFRAM gets its own permission domain. Lower FRAM stays
 * R+X (no W) via segments 1 and 2; HIFRAM gets R+W+X via segment 3.
 *
 * Addresses are shifted right by 4 before being written to the
 * MPUSEGBx registers (handled in arch/msp430/tiku_mpu_arch.c).
 */
#define TIKU_DEVICE_MPU_SEG2_START  0x8000U
#define TIKU_DEVICE_MPU_SEG3_START  0x10000UL

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

/*
 * External analog input pin per ADC12_B channel, encoded as (port << 4) | bit.
 * This map differs sharply from the FR59xx parts -- only A0-A3 share their
 * assignment -- so assuming that layout here silently muxes the wrong pins.
 */
#define TIKU_DEVICE_ADC_PIN_MAP                                     \
    { 0x10, 0x11, 0x12, 0x13,   /* A0-A3   P1.0-P1.3 */             \
      0x87, 0x86, 0x85, 0x84,   /* A4-A7   P8.7-P8.4 (descending) */\
      0x90, 0x91, 0x92, 0x93,   /* A8-A11  P9.0-P9.3 */             \
      0x94, 0x95, 0x96, 0x97 }  /* A12-A15 P9.4-P9.7 */

/*---------------------------------------------------------------------------*/
/* LCD CONTROLLER                                                            */
/*---------------------------------------------------------------------------*/

/*
 * The FR6989 carries the LCD_C segment driver: up to 320 segments with a
 * built-in charge pump.  The LaunchPad wires it to an on-board FH-1138P
 * 96-segment display with six alphanumeric positions plus icons.
 */
#define TIKU_DEVICE_HAS_LCD_C       1

/*---------------------------------------------------------------------------*/
/* FRAM REGION BUDGET                                                        */
/*---------------------------------------------------------------------------*/

/*
 * Per-device sizing for the NVM-backed regions; the map module declares the
 * arrays and the linker places them.  The lower window keeps the same ~48 KB
 * ceiling as the FR5969, so slots large enough to overflow it belong in HIFRAM.
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      2048U   /* Init table + credentials */

/* Future: loadable app slots (reserved IDs, not allocated until enabled) */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    8192U   /* 8 KB per app slot */
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   8       /* 8 slots on 128 KB part */

#endif /* TIKU_DEVICE_FR6989_H_ */
