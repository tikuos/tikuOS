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

/** @brief Human-readable device name string exposed via /sys/device. */
#define TIKU_DEVICE_NAME            "Apollo510"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Virtual GPIO port availability flags.
 *
 * Apollo510 has 100+ pads. Four virtual ports of 8 pins each (pads 0..31)
 * are exposed through the /dev/gpio/{1..4}/{0..7} VFS view to match the
 * MSP430/RP2350 layout. Board LEDs (pads 89/92/165) sit above this range
 * and are driven by raw pad number via the board header macros.
 */
#define TIKU_DEVICE_HAS_PORT1       1  /**< Virtual port 1 (pads 0..7). */
#define TIKU_DEVICE_HAS_PORT2       1  /**< Virtual port 2 (pads 8..15). */
#define TIKU_DEVICE_HAS_PORT3       1  /**< Virtual port 3 (pads 16..23). */
#define TIKU_DEVICE_HAS_PORT4       1  /**< Virtual port 4 (pads 24..31). */
#define TIKU_DEVICE_HAS_PORT5       0  /**< Not exposed in VFS view. */
#define TIKU_DEVICE_HAS_PORT6       0  /**< Not exposed in VFS view. */
#define TIKU_DEVICE_HAS_PORT7       0  /**< Not exposed in VFS view. */
#define TIKU_DEVICE_HAS_PORT8       0  /**< Not exposed in VFS view. */
#define TIKU_DEVICE_HAS_PORT9       0  /**< Not exposed in VFS view. */
#define TIKU_DEVICE_HAS_PORTJ       0  /**< Not exposed in VFS view. */

/*---------------------------------------------------------------------------*/
/* CRYSTAL OSCILLATOR                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Oscillator configuration for the Apollo510 EVB.
 *
 * The EVB carries a 32.768 kHz crystal (LFXT) used as the low-frequency
 * reference for STIMER and the RTC. The high-frequency clock is the
 * internal HFRC; there is no external HF crystal.
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
 * implementation at compile time. Apollo510 uses its own clock tree
 * (HFRC/HFRC2/LFRC/XTAL) — there is no MSP430-style CS key.
 */
#define TIKU_DEVICE_CS_HAS_KEY        0  /**< No CS unlock key required. */
#define TIKU_DEVICE_CS_TYPE_APOLLO510 1  /**< Select Apollo510 clock driver. */

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Maximum stable core frequency in MHz.
 *
 * HFRC2 can boost to ~250 MHz; HFRC free-runs at ~96 MHz (the default
 * after SBL hand-off). Override TIKU_MAIN_CPU_FREQ at build time to run
 * at a lower frequency for power savings.
 */
#define TIKU_DEVICE_MAX_STABLE_MHZ  250

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Primary RAM (DTCM) base address and size.
 *
 * DTCM is the primary RAM pool holding .data, .bss, the heap, and the
 * main stack. It is the lowest-latency SRAM bank on the Cortex-M55.
 */
#define TIKU_DEVICE_RAM_SIZE        (512UL * 1024UL) /**< 512 KB DTCM. */
#define TIKU_DEVICE_RAM_START       0x20000000UL     /**< DTCM base address. */

/**
 * @brief Non-volatile memory (MRAM) map.
 *
 * The "FRAM" naming follows the portable TikuOS convention. On Apollo510
 * the NVM is internal MRAM (flash). The usable region starts at 0x00410000
 * because the low 64 KB of MRAM is reserved for the Secure Bootloader (SBL).
 * Persistent storage uses an MRAM page via the NVM HAL (tiku_mem_arch.c).
 */
#define TIKU_DEVICE_FRAM_SIZE       (4128768UL)   /**< ~3.94 MB usable MRAM. */
#define TIKU_DEVICE_FRAM_START      0x00410000UL  /**< First usable MRAM addr. */
#define TIKU_DEVICE_FRAM_END        0x007FFFFFUL  /**< Last MRAM address. */

/**
 * @brief Init-table and app-slot sizing constants.
 *
 * Init-table backing region is RAM-resident at this milestone. Sized as
 * on RP2350 (4-byte header + 8 entries) with headroom rounding.
 * App slots are 4 KB pages reserved in MRAM for dynamic module storage.
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      576U   /**< Init-table region (bytes). */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    4096U  /**< One app slot size (bytes). */
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   4      /**< Number of app slots. */

/*---------------------------------------------------------------------------*/
/* MPU                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief ARMv8-M MPU availability flag.
 *
 * The Cortex-M55 includes the ARMv8-M MPU. The full W^X driver lands in
 * Milestone 2/3; the initial tiku_mpu_arch.c is a pass-through shim.
 */
#define TIKU_DEVICE_HAS_MPU         1

/*---------------------------------------------------------------------------*/
/* PERIPHERAL DEFAULTS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default UART baud rate.
 *
 * Applied when the board header does not override TIKU_BOARD_UART_BAUD.
 * 115200 bps is the standard rate for SWO and wire-UART on Apollo510.
 */
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        115200U
#endif

#endif /* TIKU_DEVICE_APOLLO510_H_ */
