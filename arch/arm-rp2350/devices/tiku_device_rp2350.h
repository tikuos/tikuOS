/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_rp2350.h - Raspberry Pi RP2350 silicon-level constants.
 *
 * A dual-core Cortex-M33 at up to 150 MHz with 520 KB SRAM and no on-chip
 * flash (the board's QSPI part is mapped XIP at 0x10000000), 30 GPIO on
 * bank 0, and the standard NVIC, SysTick and MPU.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_RP2350_H_
#define TIKU_DEVICE_RP2350_H_

#include <stdint.h>
#include <arch/arm-rp2350/tiku_rp2350_regs.h>

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable device name string. */
#define TIKU_DEVICE_NAME            "RP2350"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief GPIO port presence flags.
 *
 * No RP2350 code reads them: tiku_gpio_geometry.h fixes the /dev/gpio layout
 * as virtual ports of eight over bank 0, port 1 = GP0..7, 2 = GP8..15,
 * 3 = GP16..23 and 4 = GP24..29.
 *
 * @note Bank 0 has 48 pins on the larger packages; GP30 and above are not
 *       exposed.
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
 * @brief Crystal oscillator availability and frequency.
 *
 * The Pico 2 boards carry a 12 MHz crystal on XOSC.  The RP2350 has one
 * crystal oscillator, reported to the HAL as HFXT present and LFXT absent.
 */
#define TIKU_DEVICE_HAS_LFXT        0
#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_XOSC_HZ         12000000UL

/*---------------------------------------------------------------------------*/
/* CLOCK SYSTEM TYPE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clock system type selector flags.
 *
 * TIKU_DEVICE_CS_HAS_KEY = 0: no clock-system unlock key (the MSP430 CSCTL0_H
 * password).  TIKU_DEVICE_CS_TYPE_RP2350 names the clock system; no code
 * tests it, as hal/tiku_cpu.c picks the clock driver by PLATFORM_RP2350.
 */
#define TIKU_DEVICE_CS_HAS_KEY      0
#define TIKU_DEVICE_CS_TYPE_RP2350  1

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Maximum stable CPU frequency in MHz.
 *
 * The datasheet rates clk_sys at up to 150 MHz, which is also the
 * frequency the boot code sets.
 */
#define TIKU_DEVICE_MAX_STABLE_MHZ  150

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief On-chip SRAM size and base address.
 *
 * 520 KB unified SRAM at 0x20000000.  SRAM8 and SRAM9 are the 4 KB banks at
 * the top of the range; the whole region is one flat 520 KB pool.
 */
#define TIKU_DEVICE_RAM_SIZE        (520UL * 1024UL)
#define TIKU_DEVICE_RAM_START       0x20000000UL

/**
 * @brief External XIP flash size and address range.
 *
 * The 4 MB QSPI flash of the Pico 2 boards, under the TIKU_DEVICE_FRAM_* names
 * that the kernel's memory reports and region table read on every port.
 * Durable state is mirrored to a flash sector by tiku_mem_arch.c.
 */
#define TIKU_DEVICE_FRAM_SIZE       (4UL * 1024UL * 1024UL)
#define TIKU_DEVICE_FRAM_START      0x10000000UL
#define TIKU_DEVICE_FRAM_END        0x103FFFFFUL
#define TIKU_DEVICE_NVM_LABEL       "Flash"   /**< NVM technology (UI label). */

/**
 * @brief Init-table backing region size in bytes.
 *
 * kernel/memory/tiku_nvm_map.c places the region in TIKU_DURABLE SRAM, which
 * the flash mirror keeps.  The table is a 4-byte header plus 8 entries of 66
 * bytes, 532 bytes, rounded up to a multiple of 64.
 *
 * @note tiku_init.c fails the build when this is smaller than the table.
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      576U

/**
 * @brief Application slot size and count.
 *
 * No code reads them.
 */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    4096U
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   4

/*---------------------------------------------------------------------------*/
/* MPU                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Memory Protection Unit availability flag.
 *
 * The Cortex-M33's ARMv8-M MPU.  tiku_mpu_arch.c keeps region 0, the .uninit
 * (durable) range, read-only outside an unlock_nvm / lock_nvm window; a write
 * there faults, and the MemManage handler counts it and resets the chip.
 */
#define TIKU_DEVICE_HAS_MPU         1

/*---------------------------------------------------------------------------*/
/* PERIPHERAL DEFAULTS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default UART baud rate for the Pico console.
 *
 * Matches the Pico SDK default, picotool, the Debug Probe, and OpenOCD
 * documentation. Override via -DTIKU_BOARD_UART_BAUD=... on the make line.
 */
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        115200U
#endif

#endif /* TIKU_DEVICE_RP2350_H_ */
