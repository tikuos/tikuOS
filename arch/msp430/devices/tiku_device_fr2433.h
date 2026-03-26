/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_fr2433.h - MSP430FR2433 silicon-level constants
 *
 * This header defines the hardware capabilities of the MSP430FR2433
 * microcontroller: available GPIO ports, crystal support, memory
 * sizes, and peripheral availability. Board-level (PCB) definitions
 * such as LED and button pin assignments belong in the board header.
 *
 * Key differences from FR5969/FR5994:
 *   - No external crystal oscillators (no LFXT, no HFXT)
 *   - Only 3 GPIO ports (P1, P2, P3) — no P4 and no PJ
 *   - CS module has no password protection (no CSKEY)
 *   - DCO uses DCORSEL (3-bit) instead of DCOFSEL + DCORSEL
 *   - MCLK and SMCLK share a combined source select (SELMS)
 *   - 16 KB FRAM, 4 KB SRAM
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

#ifndef TIKU_DEVICE_FR2433_H_
#define TIKU_DEVICE_FR2433_H_

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_NAME            "MSP430FR2433"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_HAS_PORT1       1
#define TIKU_DEVICE_HAS_PORT2       1
#define TIKU_DEVICE_HAS_PORT3       1
#define TIKU_DEVICE_HAS_PORT4       0
#define TIKU_DEVICE_HAS_PORT5       0
#define TIKU_DEVICE_HAS_PORT6       0
#define TIKU_DEVICE_HAS_PORT7       0
#define TIKU_DEVICE_HAS_PORT8       0
#define TIKU_DEVICE_HAS_PORT9       0
#define TIKU_DEVICE_HAS_PORTJ       0

/*---------------------------------------------------------------------------*/
/* CRYSTAL OSCILLATOR AVAILABILITY                                           */
/*---------------------------------------------------------------------------*/

/** FR2433 has no external LFXT or HFXT crystal support */
#define TIKU_DEVICE_HAS_LFXT        0
#define TIKU_DEVICE_HAS_HFXT        0

/*---------------------------------------------------------------------------*/
/* CLOCK SYSTEM TYPE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * FR2433 uses the CS module with FLL (different from FR5969's CS_A).
 * Key differences:
 *   - No CSKEY password protection
 *   - DCO frequency set via DCORSEL (3-bit, in CSCTL1)
 *   - MCLK+SMCLK source: SELMS (combined, in CSCTL4)
 *   - ACLK source: SELA (1-bit, in CSCTL4): XT1CLK or REFOCLK
 *   - Dividers in CSCTL5 (not CSCTL3)
 */
#define TIKU_DEVICE_CS_HAS_KEY      0
#define TIKU_DEVICE_CS_TYPE_FR2X33  1

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_MAX_STABLE_MHZ  16

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_FRAM_SIZE       (16 * 1024UL)   /* 16 KB FRAM (15.5 KB usable) */
#define TIKU_DEVICE_RAM_SIZE        (4 * 1024UL)    /* 4 KB SRAM */
#define TIKU_DEVICE_RAM_START       0x2000U         /* First byte of SRAM */

/*---------------------------------------------------------------------------*/
/* FRAM ADDRESS RANGE                                                        */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_FRAM_START      0xC400U  /* First byte of main FRAM */
#define TIKU_DEVICE_FRAM_END        0xFFFFU  /* Last byte of main FRAM */

/*---------------------------------------------------------------------------*/
/* MPU SEGMENT BOUNDARIES                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_MPU_SEG2_START  0xD800U
#define TIKU_DEVICE_MPU_SEG3_START  0xF000U

/*---------------------------------------------------------------------------*/
/* FR2433 DCO RANGE SELECT VALUES                                            */
/*---------------------------------------------------------------------------*/

/**
 * The toolchain header defines DCORSEL_0 through DCORSEL_5.
 * DCORSEL_6 (8 MHz) and DCORSEL_7 (16 MHz) are valid but not
 * pre-defined in some header versions. Define them as fallbacks.
 */
#ifndef DCORSEL_6
#define DCORSEL_6               (0x000C)    /* DCO range: 8 MHz nominal */
#endif

#ifndef DCORSEL_7
#define DCORSEL_7               (0x000E)    /* DCO range: 16 MHz nominal */
#endif

#endif /* TIKU_DEVICE_FR2433_H_ */
