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

/* The AXI SRAM array is one contiguous span from 0x34000000 to 0x343C0000 --
 * 3.75 MB, measured on silicon rather than taken from a datasheet total, since
 * a write above the top bus-faults. The boot ROM loads the image into a 255 KB
 * window part-way up it (0x34180400), so code, data and stack occupy that
 * window while the tier arena takes the 2 MB above it. */
#define TIKU_DEVICE_RAM_START       0x34000000UL
#define TIKU_DEVICE_RAM_SIZE        0x3C0000UL

/* The window the boot ROM copies into, which is what .data/.bss/stack fit in
 * and what the linker script bounds; the rest of the array is reached through
 * the region table rather than by linking into it. */
#define TIKU_DEVICE_IMAGE_WINDOW_START  0x34180400UL
#define TIKU_DEVICE_IMAGE_WINDOW_SIZE   0x3FC00UL

/* No internal NVM: the durable store is the 64 MB Macronix NOR on XSPI2,
 * described here by its memory-mapped window. Nothing of the image lives
 * there -- code runs from SRAM -- so the in-use figure derived from _etext
 * correctly comes out zero. */
#define TIKU_DEVICE_FRAM_SIZE       0x04000000UL
#define TIKU_DEVICE_FRAM_START      0x70000000UL
#define TIKU_DEVICE_FRAM_END        0x73FFFFFFUL
#define TIKU_DEVICE_NVM_LABEL       "NOR"

#define TIKU_DEVICE_HAS_MPU         1

#define TIKU_BOARD_UART_BAUD        115200U

#endif /* TIKU_DEVICE_STM32N6_H_ */
