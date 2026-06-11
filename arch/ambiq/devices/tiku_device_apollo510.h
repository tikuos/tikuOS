/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_apollo510.h - Ambiq Apollo 510 silicon-level constants
 *
 * Apollo 510 is an Arm Cortex-M55 (ARMv8.1-M, FPU + Helium) MCU with:
 *   - 512 KB DTCM at 0x20000000 (primary RAM pool) + 3 MB shared SRAM
 *     at 0x20080000 + 256 KB ITCM at 0x0.
 *   - 4 MB internal MRAM (flash) at 0x00400000; the application image
 *     lives above the SBL at 0x00410000.
 *   - 100+ GPIO pads; standard ARM peripherals (NVIC, SysTick, MPU).
 *
 * Pure constants — no SDK/CMSIS include. The arch .c files pull in only the
 * bare CMSIS register header (apollo510.h); no AmbiqSuite HAL/BSP remains.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_APOLLO510_H_
#define TIKU_DEVICE_APOLLO510_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_NAME            "Apollo510"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/*
 * Apollo510 has 100+ pads. We expose the low four virtual "ports" of 8
 * pins each (pads 0..31) through the /dev/gpio/{1..4}/{0..7} VFS view, to
 * match the MSP430/RP2350 layout. Board LEDs (pads 89/92/165) sit above
 * this range and are driven by raw pad number (see the board header).
 */
#define TIKU_DEVICE_HAS_PORT1       1
#define TIKU_DEVICE_HAS_PORT2       1
#define TIKU_DEVICE_HAS_PORT3       1
#define TIKU_DEVICE_HAS_PORT4       1
#define TIKU_DEVICE_HAS_PORT5       0
#define TIKU_DEVICE_HAS_PORT6       0
#define TIKU_DEVICE_HAS_PORT7       0
#define TIKU_DEVICE_HAS_PORT8       0
#define TIKU_DEVICE_HAS_PORT9       0
#define TIKU_DEVICE_HAS_PORTJ       0

/*---------------------------------------------------------------------------*/
/* CRYSTAL OSCILLATOR                                                        */
/*---------------------------------------------------------------------------*/

/* The EVB carries a 32.768 kHz crystal (LFXT). The high-frequency clock is
 * the internal HFRC — there is no external HF crystal. */
#define TIKU_DEVICE_HAS_LFXT        1
#define TIKU_DEVICE_HAS_HFXT        0
#define TIKU_DEVICE_XOSC_HZ         32768UL

/*---------------------------------------------------------------------------*/
/* CLOCK SYSTEM TYPE                                                         */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_CS_HAS_KEY        0
#define TIKU_DEVICE_CS_TYPE_APOLLO510 1

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

/* HFRC2 can boost to ~250 MHz; HFRC free-runs ~96 MHz (the default). */
#define TIKU_DEVICE_MAX_STABLE_MHZ  250

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/* DTCM is the primary RAM pool (.data/.bss/heap/stack). */
#define TIKU_DEVICE_RAM_SIZE        (512UL * 1024UL)
#define TIKU_DEVICE_RAM_START       0x20000000UL

/*
 * "FRAM" names map to internal MRAM (flash). The usable region starts at
 * 0x00410000 (the low 64 KB of MRAM is reserved for the SBL). Persistent
 * storage uses an MRAM page via the NVM HAL (see tiku_mem_arch.c).
 */
#define TIKU_DEVICE_FRAM_SIZE       (4128768UL)      /* ~3.94 MB usable    */
#define TIKU_DEVICE_FRAM_START      0x00410000UL
#define TIKU_DEVICE_FRAM_END        0x007FFFFFUL

/* Init-table backing region (RAM-resident at this milestone). Sized as on
 * RP2350: 4-byte header + 8 entries; rounded up for headroom. */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      576U

#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    4096U
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   4

/*---------------------------------------------------------------------------*/
/* MPU                                                                       */
/*---------------------------------------------------------------------------*/

/* Cortex-M55 has the ARMv8-M MPU. (Full driver lands in Milestone 2/3; the
 * initial tiku_mpu_arch.c is a pass-through shim.) */
#define TIKU_DEVICE_HAS_MPU         1

/*---------------------------------------------------------------------------*/
/* PERIPHERAL DEFAULTS                                                       */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        115200U
#endif

#endif /* TIKU_DEVICE_APOLLO510_H_ */
