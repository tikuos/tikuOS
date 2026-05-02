/*
 * Tiku Operating System v0.03
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

#ifndef TIKU_DEVICE_FR5994_H_
#define TIKU_DEVICE_FR5994_H_

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_NAME            "MSP430FR5994"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

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

#define TIKU_DEVICE_FRAM_SIZE       (256 * 1024UL)  /* 256 KB FRAM */

/*
 * MSP430FR5994 has 8 KB physical SRAM split by the toolchain's stock
 * linker script into RAM (4 KB) + LEARAM (~3.7 KB) + LEASTACK (312 B).
 * When LEA is not used (the TikuOS default, controlled by the
 * `LEA_ENABLE` Makefile flag), the project-local LD override merges
 * all three into one 8 KB RAM region, doubling general-purpose SRAM.
 *
 * TIKU_FR5994_LEA_DISABLED is defined by the Makefile when
 * LEA_ENABLE=0 so this header can report the actually-available
 * RAM size to higher layers (`free`, /sys/mem, hibernate budgeting,
 * etc.). Build with `make MCU=msp430fr5994 LEA_ENABLE=1` to keep
 * the stock 4 KB layout if you bring up an LEA-using driver.
 */
#ifdef TIKU_FR5994_LEA_DISABLED
#define TIKU_DEVICE_RAM_SIZE        (8 * 1024UL)    /* 8 KB merged (default) */
#else
#define TIKU_DEVICE_RAM_SIZE        (4 * 1024UL)    /* 4 KB, LEA gets 4 KB */
#endif
#define TIKU_DEVICE_RAM_START       0x1C00U         /* First byte of SRAM */

/*---------------------------------------------------------------------------*/
/* FRAM REGION SIZING (used by kernel/memory/tiku_fram_map)                  */
/*---------------------------------------------------------------------------*/

/*
 * Per-device sizing for FRAM-backed regions.  The kernel/memory/tiku_fram_map
 * module reads these to declare storage arrays; the linker places them.
 * Adjust sizes per device — the rest of the system adapts automatically.
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      1024U   /* Init table + credentials */

/* Future: loadable app slots (reserved IDs, not allocated until enabled) */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    8192U   /* 8 KB per app slot */
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   8       /* 8 slots on 256 KB part */

/*---------------------------------------------------------------------------*/
/* FRAM ADDRESS RANGE                                                        */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_FRAM_START      0x4000U  /* First byte of main FRAM */
#define TIKU_DEVICE_FRAM_END        0xFFFFU  /* Last byte of lower 64 KB */

/*
 * The remaining ~192 KB of FRAM lives at 0x10000+ (HIFRAM). Reach it for
 * *data* via the TIKU_HIFRAM* macros in <kernel/memory/tiku_mem.h>; code
 * only goes up there with MEMORY_MODEL=large.
 */
#define TIKU_DEVICE_HAS_HIFRAM      1
#define TIKU_DEVICE_HIFRAM_START    0x10000UL  /* First byte of HIFRAM */
#define TIKU_DEVICE_HIFRAM_END      0x43FF6UL  /* Last byte of HIFRAM (~208 KB) */

/*---------------------------------------------------------------------------*/
/* MPU (MEMORY PROTECTION UNIT)                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_MPU         1   /**< FR5994 has hardware MPU */

/*
 * Segment 3 covers HIFRAM only so MEMORY_MODEL=large can place
 * .upper.bss / .upper.data there with R+W+X while segments 1-2 keep
 * lower FRAM (code, vectors, persistent data) at R+X. Without this,
 * granting W on segment 3 to permit large-mode kernel writes would
 * also grant W on lower-FRAM code at 0xC000-0xFFFF.
 */
#define TIKU_DEVICE_MPU_SEG2_START  0x8000U
#define TIKU_DEVICE_MPU_SEG3_START  0x10000UL

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
#define TIKU_DEVICE_ADC_CHANNELS    32  /**< External channels A0-A31 */

#endif /* TIKU_DEVICE_FR5994_H_ */
