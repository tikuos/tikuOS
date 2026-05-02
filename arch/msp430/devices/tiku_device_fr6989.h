/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_fr6989.h - MSP430FR6989 silicon-level constants
 *
 * This header defines the hardware capabilities of the MSP430FR6989
 * microcontroller: available GPIO ports, crystal pin routing, memory
 * sizes, and peripheral availability. Board-level (PCB) definitions
 * such as LED and button pin assignments belong in the board header.
 *
 * Family notes:
 *   - 100-pin LQFP, ~76 GPIO pins
 *   - 128 KB FRAM, 2 KB SRAM
 *   - LCD_C segment-LCD driver on chip (drives the on-board LCD glass
 *     of MSP-EXP430FR6989; not used by TikuOS, kept disabled at boot)
 *   - HFXT pins on PJ.6/PJ.7 (matches FR5994, NOT FR5969 which uses
 *     PJ.2/PJ.3)
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

#ifndef TIKU_DEVICE_FR6989_H_
#define TIKU_DEVICE_FR6989_H_

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_NAME            "MSP430FR6989"

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

/*---------------------------------------------------------------------------*/
/* LCD CONTROLLER                                                            */
/*---------------------------------------------------------------------------*/

/**
 * FR6989 has the LCD_C segment-LCD driver: up to 320 segments
 * (8 mux × 40 pins) with built-in charge pump for 3.0/3.3V drive.
 * The MSP-EXP430FR6989 LaunchPad wires it to an on-board FH-1138P
 * 96-segment display (4-mux, six 14-segment alphanumeric positions
 * plus various icons).
 */
#define TIKU_DEVICE_HAS_LCD_C       1

/*---------------------------------------------------------------------------*/
/* FRAM REGION BUDGET                                                        */
/*---------------------------------------------------------------------------*/

/**
 * Per-device sizing for FRAM-backed regions. The kernel/memory/tiku_fram_map
 * module reads these to declare storage arrays; the linker places them.
 * Adjust sizes per device — the rest of the system adapts automatically.
 *
 * 128 KB FRAM: lower window is the same ~48 KB code/data ceiling as
 * FR5969, so the budgets below reflect what the larger HIFRAM unlocks
 * for slot-style data. Slots large enough to overflow lower FRAM should
 * be placed via TIKU_HIFRAM* (see <kernel/memory/tiku_mem.h>).
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      2048U   /* Init table + credentials */

/* Future: loadable app slots (reserved IDs, not allocated until enabled) */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    8192U   /* 8 KB per app slot */
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   8       /* 8 slots on 128 KB part */

#endif /* TIKU_DEVICE_FR6989_H_ */
