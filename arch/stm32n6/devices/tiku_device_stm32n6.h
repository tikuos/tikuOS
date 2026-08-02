/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_stm32n6.h - STM32N657X0 silicon facts.
 *
 * Cortex-M55 with no internal NVM: the boot ROM loads one signed image into
 * SRAM, so the usable memory is that window and durability is volatile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_STM32N6_H_
#define TIKU_DEVICE_STM32N6_H_

#define TIKU_DEVICE_NAME            "STM32N657"

/* GPIO ports are letters A..Q, and the VFS port index is that letter's
 * position, so /dev/gpio/6 is GPIOG -- the port carrying the Nucleo LEDs.
 * Index 0 (GPIOA) has no VFS node because the tree numbers ports from 1. */
#define TIKU_DEVICE_HAS_PORT1       1   /* GPIOB */
#define TIKU_DEVICE_HAS_PORT2       1   /* GPIOC */
#define TIKU_DEVICE_HAS_PORT3       1   /* GPIOD */
#define TIKU_DEVICE_HAS_PORT4       1   /* GPIOE */
#define TIKU_DEVICE_HAS_PORT5       1   /* GPIOF */
#define TIKU_DEVICE_HAS_PORT6       1   /* GPIOG */
#define TIKU_DEVICE_HAS_PORT7       1   /* GPIOH */
#define TIKU_DEVICE_HAS_PORT8       1   /* GPIOI */
#define TIKU_DEVICE_HAS_PORT9       1   /* GPIOJ */
#define TIKU_DEVICE_HAS_PORTJ       0   /* MSP430 port J has no STM32 analogue */

/* HSE value ST documents for this family; the port runs from HSI and does not
 * program the tree, so no code depends on the crystal yet. */
#define TIKU_DEVICE_HAS_LFXT        0
#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_XOSC_HZ         48000000UL
#define TIKU_DEVICE_CS_HAS_KEY      0
#define TIKU_DEVICE_CS_TYPE_STM32N6 1
#define TIKU_DEVICE_MAX_STABLE_MHZ  600

/* The image window the boot ROM loads into, per the CubeProgrammer device DB:
 * 0x34180400 for 0x3FC00 bytes. Code, data and stack all live here, so this is
 * the whole memory budget -- the part's other SRAM banks are not claimed yet. */
#define TIKU_DEVICE_RAM_START       0x34180400UL
#define TIKU_DEVICE_RAM_SIZE        0x3FC00UL

/* No internal NVM of any kind. Durability is SRAM-backed for now; the 64 Mbit
 * Macronix XSPI part on the board is the eventual home. */
#define TIKU_DEVICE_FRAM_SIZE       0UL
#define TIKU_DEVICE_FRAM_START      0UL
#define TIKU_DEVICE_FRAM_END        0UL
#define TIKU_DEVICE_NVM_LABEL       "none (SRAM)"

#define TIKU_DEVICE_HAS_MPU         1

#define TIKU_BOARD_UART_BAUD        115200U

#endif /* TIKU_DEVICE_STM32N6_H_ */
