/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_nrf54lv10a.h - Nordic nRF54LV10A silicon-level constants.
 *
 * The low-voltage (1.2-1.7 V) member of the nRF54L family, with the L15's
 * clock, RRAMC and GRTC: 192 KB SRAM, 1012 KB RRAM and two GPIO ports.
 * All-Secure, so peripherals use the _S aliases.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_NRF54LV10A_H_
#define TIKU_DEVICE_NRF54LV10A_H_

#include <stdint.h>
#include <arch/nordic/mdk/nrf54lv10a.h>
#include <arch/nordic/tiku_nordic_core.h>

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable device name string. */
#define TIKU_DEVICE_NAME            "nRF54LV10A"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Virtual GPIO port availability flags.
 *
 * Virtual ports 1 and 2 (/dev/gpio/{1,2}) are P0 (LP domain, P0.00..P0.04)
 * and P1 (PERI, P1.00..P1.25); the part has no P2 or P3.
 */
#define TIKU_DEVICE_HAS_PORT1       1   /* P0 */
#define TIKU_DEVICE_HAS_PORT2       1   /* P1 */
#define TIKU_DEVICE_HAS_PORT3       0
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

/**
 * @brief On-chip SRAM size and base address (192 KB @ 0x20000000).
 *
 * RAM (128 KB) and RAM1 (64 KB at 0x20020000) are contiguous.  MPC00
 * override regions cover 0x2002FEE0..0x2002FFFF, and the debugger cannot
 * read that span.
 */
#define TIKU_DEVICE_RAM_SIZE        (192UL * 1024UL)

/**
 * @brief App-usable SRAM: the part less its top 20 KB, the 16 KB FLPR (VPR
 *        RISC-V) coprocessor carve at 0x2002B000 and the 4 KB at 0x2002F000
 *        holding the MPC-covered span.
 *
 * Every build reserves both, and memory reports use this figure.  It must
 * equal LENGTH(SRAM) in nrf54lv10a.ld.
 */
#define TIKU_DEVICE_RAM_USABLE      (172UL * 1024UL)
#define TIKU_DEVICE_RAM_START       0x20000000UL

/**
 * @brief On-chip RRAM range, under the FRAM_* names the kernel's memory
 *        reports and the NVM region table use.
 *
 * 0xFD000 bytes (1012 KB, FICR.INFO.RRAM) at 0x0 hold code and the
 * persistent region; an access from 0xFD000 up faults.  RRAM is written in
 * place behind RRAMC WEN.
 */
#define TIKU_DEVICE_FRAM_SIZE       0x000FD000UL
#define TIKU_DEVICE_FRAM_START      0x00000000UL
#define TIKU_DEVICE_FRAM_END        0x000FCFFFUL
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

#endif /* TIKU_DEVICE_NRF54LV10A_H_ */
