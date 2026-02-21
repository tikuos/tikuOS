/*
 * Tiku Operating System
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

#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_MAX_STABLE_MHZ  16

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_FRAM_SIZE       (256 * 1024UL)  /* 256 KB FRAM */
#define TIKU_DEVICE_RAM_SIZE        (8 * 1024UL)    /* 8 KB SRAM */

#endif /* TIKU_DEVICE_FR5994_H_ */
