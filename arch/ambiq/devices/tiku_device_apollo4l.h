/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_apollo4l.h - Ambiq Apollo4 silicon constants.
 *
 * Used for the Apollo4 Lite and Plus: a Cortex-M4F with 384 KB TCM, shared
 * SRAM above it, and 2 MB MRAM at 0x0 with the image above a reserved low
 * region.  Constants only.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_APOLLO4L_H_
#define TIKU_DEVICE_APOLLO4L_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Device name, read by /sys/device/mcu. */
#define TIKU_DEVICE_NAME            "Apollo4L"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief MSP430-style GPIO port flags; the Ambiq build does not read them.
 *
 * /dev/gpio takes its ports from tiku_gpio_geometry.h: ports 1..16 of eight
 * pads cover pads 0..127, with pad = (port - 1) * 8 + pin.
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
 *        runs from the internal HFRC.  The Ambiq build reads none of these.
 */
#define TIKU_DEVICE_HAS_LFXT        1        /**< 32.768 kHz LFXT present. */
#define TIKU_DEVICE_HAS_HFXT        0        /**< No external HF crystal. */
#define TIKU_DEVICE_XOSC_HZ         32768UL  /**< LFXT frequency in Hz. */

/*---------------------------------------------------------------------------*/
/* CLOCK SYSTEM TYPE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clock-system selectors.
 *
 * There is no MSP430-style CS unlock key.  TIKU_DEVICE_CS_TYPE_APOLLO4L
 * selects the 128-pad Apollo4 port table in tiku_gpio_geometry.h.
 */
#define TIKU_DEVICE_CS_HAS_KEY        0  /**< No CS unlock key required. */
#define TIKU_DEVICE_CS_TYPE_APOLLO4L  1  /**< Apollo4 part (Lite or Plus). */

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Core clock in low-power mode, in MHz; no code reads it.
 *
 * MAIN_CPU_FREQ above 96 selects the 192 MHz high-performance mode.
 */
#define TIKU_DEVICE_MAX_STABLE_MHZ  96

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Primary RAM: the 384 KB TCM at 0x10000000.
 *
 * TCM holds .data, .bss, the durable .uninit image and the main stack.  The
 * shared SRAM follows at 0x10060000, 1 MB in apollo4l.ld and 2 MB in
 * apollo4p.ld.
 */
#define TIKU_DEVICE_RAM_SIZE        (384UL * 1024UL) /**< 384 KB TCM. */
#define TIKU_DEVICE_RAM_START       0x10000000UL     /**< TCM base address. */

/**
 * @brief Non-volatile memory map: 2 MB of MRAM at 0x0.
 *
 * The portable TIKU_DEVICE_FRAM_* names describe whatever the part's NVM is;
 * here it is MRAM.  The low 96 KB, below 0x18000, is reserved for boot code.
 */
#define TIKU_DEVICE_FRAM_SIZE       (1998848UL)   /**< MRAM above 0x18000. */
#define TIKU_DEVICE_FRAM_START      0x00018000UL  /**< First usable byte. */
#define TIKU_DEVICE_FRAM_END        0x001FFFFFUL  /**< Last MRAM byte. */
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
 * @brief The Cortex-M4F has an ARMv7-M (PMSAv7) MPU with 8 regions.
 *
 * tiku_mpu_apollo4l.c programs it in the kernel build; a MINIMAL=1 build
 * leaves it off.
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

#endif /* TIKU_DEVICE_APOLLO4L_H_ */
