/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_apollo510.h - Ambiq Apollo510 silicon constants.
 *
 * Used for the Apollo510 and Apollo510B: a Cortex-M55 with 512 KB DTCM, 3 MB
 * shared SRAM and 256 KB ITCM, plus 4 MB MRAM with the image above the
 * secure bootloader.  Constants only.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_APOLLO510_H_
#define TIKU_DEVICE_APOLLO510_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Device name, read by /sys/device/mcu. */
#define TIKU_DEVICE_NAME            "Apollo510"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief MSP430-style GPIO port flags; the Ambiq build does not read them.
 *
 * /dev/gpio takes its ports from tiku_gpio_geometry.h: ports 1..28 of eight
 * pads cover pads 0..223, with pad = (port - 1) * 8 + pin.
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
 * @brief Oscillators: a 32.768 kHz crystal (LFXT) clocks STIMER, and the core
 *        runs from the internal HFRC and HFRC2.
 *
 * The Ambiq build reads none of these.  A board's high-speed crystal for USB
 * is declared in BOARD_CAPS (USBHS_CLK_XTAL), not here.
 */
#define TIKU_DEVICE_HAS_LFXT        1        /**< 32.768 kHz LFXT present. */
#define TIKU_DEVICE_HAS_HFXT        0        /**< Core uses no HF crystal. */
#define TIKU_DEVICE_XOSC_HZ         32768UL  /**< LFXT frequency in Hz. */

/*---------------------------------------------------------------------------*/
/* CLOCK SYSTEM TYPE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clock-system selectors.
 *
 * There is no MSP430-style CS unlock key.  No code reads
 * TIKU_DEVICE_CS_TYPE_APOLLO510.
 */
#define TIKU_DEVICE_CS_HAS_KEY        0  /**< No CS unlock key required. */
#define TIKU_DEVICE_CS_TYPE_APOLLO510 1  /**< Apollo510 clock tree. */

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Core clock in high-performance mode (HFRC2), in MHz; no code reads it.
 *
 * The core boots at 96 MHz in low-power mode; MAIN_CPU_FREQ above 96
 * selects high-performance mode.
 */
#define TIKU_DEVICE_MAX_STABLE_MHZ  250

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Primary RAM: the 512 KB DTCM at 0x20000000, and the 3 MB SSRAM.
 *
 * DTCM holds .data, .bss, the durable .uninit image and the main stack.  The
 * SSRAM holds the large static buffers and the SRAM tier.
 */
#define TIKU_DEVICE_RAM_SIZE        (512UL * 1024UL) /**< 512 KB DTCM. */
#define TIKU_DEVICE_RAM_START       0x20000000UL     /**< DTCM base address. */
#define TIKU_DEVICE_RAM2_START      0x20080000UL     /**< SSRAM base. */
#define TIKU_DEVICE_RAM2_SIZE       (3UL * 1024UL * 1024UL) /**< 3 MB SSRAM */

/**
 * @brief Non-volatile memory map: 4 MB of MRAM at 0x00400000.
 *
 * The portable TIKU_DEVICE_FRAM_* names describe whatever the part's NVM is;
 * here it is MRAM.  The low 64 KB, below 0x410000, holds the Secure
 * Bootloader.
 */
#define TIKU_DEVICE_FRAM_SIZE       (4128768UL)   /**< MRAM above 0x410000. */
#define TIKU_DEVICE_FRAM_START      0x00410000UL  /**< First usable byte. */
#define TIKU_DEVICE_FRAM_END        0x007FFFFFUL  /**< Last MRAM byte. */
#define TIKU_DEVICE_NVM_LABEL       "MRAM"        /**< Name shown to users. */

/**
 * @brief Init-table region and app-slot sizes.
 *
 * tiku_nvm_map.c backs the init table with a TIKU_DURABLE array of
 * TIKU_DEVICE_FRAM_CONFIG_SIZE bytes.  No code reads the app-slot sizes.
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      576U   /**< Init table, bytes. */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    4096U  /**< One app slot, bytes. */
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   4      /**< Number of app slots. */

/*---------------------------------------------------------------------------*/
/* MPU                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief The Cortex-M55 has an ARMv8-M MPU.
 *
 * tiku_mpu_arch.c programs it in the kernel build; a MINIMAL=1 build leaves
 * it off.
 */
#define TIKU_DEVICE_HAS_MPU         1

/*---------------------------------------------------------------------------*/
/* PERIPHERAL DEFAULTS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console baud rate unless the board header sets one.
 */
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        115200U
#endif

#endif /* TIKU_DEVICE_APOLLO510_H_ */
