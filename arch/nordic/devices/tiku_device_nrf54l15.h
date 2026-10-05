/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_nrf54l15.h - Nordic nRF54L15 silicon-level constants.
 *
 * A Cortex-M33 wireless MCU: 256 KB SRAM, 1.5 MB write-in-place RRAM at 0x0
 * holding code and the persistent region, three GPIO ports, and the GRTC as
 * tick source.  The device runs All-Secure, so peripherals use the _S aliases.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_NRF54L15_H_
#define TIKU_DEVICE_NRF54L15_H_

#include <stdint.h>
#include <arch/nordic/mdk/nrf54l15.h>
#include <arch/nordic/tiku_nordic_core.h>

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable device name string. */
#define TIKU_DEVICE_NAME            "nRF54L15"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Virtual GPIO port availability flags.
 *
 * Three physical GPIO ports map to virtual ports 1..3 for /dev/gpio/{1..3}:
 * port 1 = P0 (LP domain, P0.00..P0.04), port 2 = P1 (P1.00..P1.15), port 3 =
 * P2 (P2.00..P2.10).  tiku_gpio_arch.c maps each to its register block.
 */
#define TIKU_DEVICE_HAS_PORT1       1   /* P0 */
#define TIKU_DEVICE_HAS_PORT2       1   /* P1 */
#define TIKU_DEVICE_HAS_PORT3       1   /* P2 */
#define TIKU_DEVICE_HAS_PORT4       0
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
 * The DK fits a 32 MHz HFXO (the high-frequency source) and a 32.768 kHz
 * LFXO (LFCLK, GRTC).
 */
#define TIKU_DEVICE_HAS_LFXT        1
#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_XOSC_HZ         32000000UL

/*---------------------------------------------------------------------------*/
/* CLOCK SYSTEM TYPE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clock system type flags: no unlock key, Nordic clock system.
 *
 * The Makefile compiles the Nordic clock driver, tiku_cpu_freq_boot_arch.c,
 * for every Nordic part; it sets the core to 64 or 128 MHz once at boot.
 */
#define TIKU_DEVICE_CS_HAS_KEY      0
#define TIKU_DEVICE_CS_TYPE_NORDIC  1

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

/** @brief Maximum stable CPU frequency in MHz (datasheet). */
#define TIKU_DEVICE_MAX_STABLE_MHZ  128

/**
 * @brief Core frequency of a default build, in Hz.
 *
 * The boot sets the PLL to TIKU_NORDIC_CPU_MHZ (128 unless overridden) or to
 * a saved rate.  Delays and tiku_cpu_mclk_hz() read the running rate from
 * the PLL.
 */
#define TIKU_DEVICE_BOOT_CPU_HZ     128000000UL

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/** @brief On-chip SRAM size and base address (256 KB @ 0x20000000). */
#define TIKU_DEVICE_RAM_SIZE        (256UL * 1024UL)

/**
 * @brief App-usable SRAM: the bank less its top 16 KB, the FLPR (VPR RISC-V)
 *        coprocessor carve at 0x2003C000..0x2003FFFF.
 *
 * Every build reserves the carve, and memory reports use this figure.  It
 * must equal LENGTH(SRAM) in nrf54l15.ld.
 */
#define TIKU_DEVICE_RAM_USABLE      (240UL * 1024UL)
#define TIKU_DEVICE_RAM_START       0x20000000UL

/**
 * @brief On-chip RRAM range, under the FRAM_* names the kernel's memory
 *        reports and the NVM region table use.
 *
 * 0x17D000 bytes (1524 KB) at 0x0 hold code and the persistent region; the
 * top 12 KB of the 1.5 MB array is reserved (MDK NRF_MEMORY_FLASH_SIZE) and
 * bus-faults if addressed.  RRAM is written in place behind RRAMC WEN.
 */
#define TIKU_DEVICE_FRAM_SIZE       0x0017D000UL
#define TIKU_DEVICE_FRAM_START      0x00000000UL
#define TIKU_DEVICE_FRAM_END        0x0017CFFFUL
#define TIKU_DEVICE_NVM_LABEL       "RRAM"   /**< NVM technology (UI label). */

/**
 * @brief Init-table backing region size in bytes.
 *
 * Holds the init table: a 4-byte header and TIKU_INIT_MAX_ENTRIES (8)
 * entries; tiku_init.c asserts the fit at build time.
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      576U

/** @brief Application slot size and count in RRAM; no code reads them. */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    4096U
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   4

/*---------------------------------------------------------------------------*/
/* MPU                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Memory Protection Unit availability flag.
 *
 * The Cortex-M33 has the ARMv8-M MPU.  The RRAMC WEN gate is this port's
 * NVM write barrier; tiku_mpu_arch.c programs the MPU for the stack guard
 * and execute-never SRAM.
 */
#define TIKU_DEVICE_HAS_MPU         1

/*---------------------------------------------------------------------------*/
/* PERIPHERAL DEFAULTS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default UART baud rate for the DK console (VCOM).
 *
 * Override via -DTIKU_BOARD_UART_BAUD=... on the make line.
 */
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        115200U
#endif

#endif /* TIKU_DEVICE_NRF54L15_H_ */
