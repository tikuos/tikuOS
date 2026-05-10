/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_rp2350.h - Raspberry Pi RP2350 silicon-level constants
 *
 * RP2350 (used on Raspberry Pi Pico 2 / Pico 2 W) is a dual-core
 * Arm Cortex-M33 (or RISC-V Hazard3) MCU at up to 150 MHz with:
 *   - 520 KB on-chip SRAM (10 banks: SRAM0..7 4 MB-style + SRAM8/9
 *     boot SRAM); flat user-visible region from 0x20000000.
 *   - No on-chip Flash; the Pico 2 board ships 4 MB external QSPI
 *     PSRAM/Flash mapped XIP at 0x10000000.
 *   - 30 GPIO pins on bank 0 (GP0..GP29 on Pico 2) + extra pads on
 *     QSPI bank for the chip-select / clock to flash.
 *   - Standard ARM peripherals: NVIC, SysTick, MPU.
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

#define TIKU_DEVICE_NAME            "RP2350"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/*
 * RP2350 has a single user-visible GPIO bank (bank 0) with 48 pins
 * on the larger packages. We model it as four virtual "ports" of
 * 8 pins each so the existing /dev/gpio/{1..4}/{0..7} VFS layout
 * keeps working: port 1 = GP0..7, port 2 = GP8..15, port 3 = GP16..23,
 * port 4 = GP24..31. Bank 0 actually has more pins but only the
 * lowest 32 are exposed via the per-port VFS view.
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

/* The Pico 2 board ships a 12 MHz crystal on XOSC. The RP2350 has no
 * MSP430-style LFXT/HFXT split, but for HAL compatibility we report
 * "HFXT present, no LFXT". */
#define TIKU_DEVICE_HAS_LFXT        0
#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_XOSC_HZ         12000000UL

/*---------------------------------------------------------------------------*/
/* CLOCK SYSTEM TYPE                                                         */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_CS_HAS_KEY      0
#define TIKU_DEVICE_CS_TYPE_RP2350  1

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

/* Datasheet maxes the system PLL at 150 MHz for the production
 * silicon. That's our default target. */
#define TIKU_DEVICE_MAX_STABLE_MHZ  150

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/* 520 KB unified SRAM at 0x20000000. The boot SRAMs (SRAM8/9) are
 * 4 KB each at the top of the address range; we treat the whole
 * region as one flat 520 KB pool. */
#define TIKU_DEVICE_RAM_SIZE        (520UL * 1024UL)
#define TIKU_DEVICE_RAM_START       0x20000000UL

/*
 * "FRAM" on RP2350 is the external XIP flash (4 MB on the Pico 2
 * board). We expose it under the FRAM_* names so the kernel's
 * memory introspection / region table speaks one vocabulary. The
 * device has no FRAM in the MSP430 sense; persistent storage uses
 * a flash sector via the NVM HAL (see arch/arm-rp2350/tiku_mem_arch.c).
 */
#define TIKU_DEVICE_FRAM_SIZE       (4UL * 1024UL * 1024UL)
#define TIKU_DEVICE_FRAM_START      0x10000000UL
#define TIKU_DEVICE_FRAM_END        0x103FFFFFUL

/* Init-table backing region. RAM-resident on this port (volatile) —
 * a future revision can move it to a dedicated flash sector. */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      512U

#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    4096U
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   4

/*---------------------------------------------------------------------------*/
/* MPU                                                                       */
/*---------------------------------------------------------------------------*/

/* RP2350's Cortex-M33 has the ARMv8-M MPU. We don't program it on
 * the first port — kernel MPU calls become no-ops. */
#define TIKU_DEVICE_HAS_MPU         0

/*---------------------------------------------------------------------------*/
/* PERIPHERAL DEFAULTS                                                       */
/*---------------------------------------------------------------------------*/

/* Default HAL UART baud rate for the Pico console (matches Pico SDK
 * default). Override via -DTIKU_BOARD_UART_BAUD=... on the make line. */
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        115200U
#endif

#endif /* TIKU_DEVICE_RP2350_H_ */
