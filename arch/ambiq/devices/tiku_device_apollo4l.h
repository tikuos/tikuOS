/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_apollo4l.h - Ambiq Apollo4 Lite silicon-level constants.
 *
 * A Cortex-M4F with 384 KB TCM plus 1 MB contiguous shared SRAM, and 2 MB MRAM at
 * 0x0 with the application image above the reserved low region.  Pure constants:
 * the arch files pull in only the bare CMSIS register header.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_APOLLO4L_H_
#define TIKU_DEVICE_APOLLO4L_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable device name string exposed via /sys/device. */
#define TIKU_DEVICE_NAME            "Apollo4L"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief MSP430-style GPIO port presence flags.
 *
 * /dev/gpio takes its layout from tiku_gpio_geometry.h rather than from these
 * flags: virtual ports 1..16 of eight pads each cover pads 0..127, with
 * pad = (port - 1) * 8 + pin.  The EVB LEDs (pads 12/13/14) are on port 2.
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

/**
 * @brief Oscillator configuration for the Apollo4 Lite EVB.
 *
 * The EVB carries a 32.768 kHz crystal (LFXT) used as the low-frequency
 * reference for STIMER and the RTC. The high-frequency clock is the internal
 * HFRC; there is no external HF crystal.
 */
#define TIKU_DEVICE_HAS_LFXT        1        /**< 32.768 kHz LFXT present. */
#define TIKU_DEVICE_HAS_HFXT        0        /**< No external HF crystal. */
#define TIKU_DEVICE_XOSC_HZ         32768UL  /**< LFXT frequency in Hz. */

/*---------------------------------------------------------------------------*/
/* CLOCK SYSTEM TYPE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clock system type selectors.
 *
 * Used by the portable clock HAL to dispatch to the correct arch
 * implementation at compile time. Apollo4 Lite uses the Ambiq clock tree
 * (HFRC/LFRC/XTAL); there is no MSP430-style CS key, and no HFRC2 turbo.
 */
#define TIKU_DEVICE_CS_HAS_KEY        0  /**< No CS unlock key required. */
#define TIKU_DEVICE_CS_TYPE_APOLLO4L  1  /**< Select Apollo4 Lite clock driver. */

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Maximum stable core frequency in MHz.
 *
 * Apollo4 Lite HFRC free-runs at ~96 MHz (no HFRC2 250 MHz turbo as on
 * Apollo510). Override TIKU_MAIN_CPU_FREQ at build time for lower power.
 */
#define TIKU_DEVICE_MAX_STABLE_MHZ  96

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Primary RAM (TCM) base address and size.
 *
 * TCM is the primary RAM pool holding .data, .bss, the heap, and the main
 * stack. On Apollo4 Lite it is 384 KB at 0x10000000, immediately followed by
 * 1 MB of shared SRAM at 0x10060000.
 */
#define TIKU_DEVICE_RAM_SIZE        (384UL * 1024UL) /**< 384 KB TCM. */
#define TIKU_DEVICE_RAM_START       0x10000000UL     /**< TCM base address. */

/**
 * @brief Non-volatile memory (MRAM) map.
 *
 * The "FRAM" naming follows the portable TikuOS convention. On Apollo4 Lite
 * the NVM is internal MRAM (flash), 2 MB based at 0x0; the usable application
 * region starts at 0x00018000 (the low 96 KB is reserved for boot/info).
 */
#define TIKU_DEVICE_FRAM_SIZE       (1998848UL)   /**< ~1.9 MB usable MRAM. */
#define TIKU_DEVICE_FRAM_START      0x00018000UL  /**< First usable MRAM addr. */
#define TIKU_DEVICE_FRAM_END        0x001FFFFFUL  /**< Last MRAM address (2 MB). */
#define TIKU_DEVICE_NVM_LABEL       "MRAM"        /**< NVM technology (UI label). */

/**
 * @brief Init-table and app-slot sizing constants (same as Apollo510).
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      576U   /**< Init-table region (bytes). */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    4096U  /**< One app slot size (bytes). */
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   4      /**< Number of app slots. */

/*---------------------------------------------------------------------------*/
/* MPU                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief ARMv7-M (PMSAv7) MPU availability flag.
 *
 * The Cortex-M4 in Apollo4 Lite includes the ARMv7-M PMSAv7 MPU (8 regions,
 * RBAR/RASR). The W^X driver (tiku_mpu_apollo4l.c) lands with the full-kernel
 * milestone; the minimal smoke build does not use it.
 */
#define TIKU_DEVICE_HAS_MPU         1

/*---------------------------------------------------------------------------*/
/* PERIPHERAL DEFAULTS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default UART baud rate (115200 bps; standard for SWO and wire-UART).
 */
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        115200U
#endif

#endif /* TIKU_DEVICE_APOLLO4L_H_ */
