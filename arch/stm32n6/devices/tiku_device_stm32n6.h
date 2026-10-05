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
 * an SRAM window, and the durable store is the external NOR on XSPI2.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_STM32N6_H_
#define TIKU_DEVICE_STM32N6_H_

#define TIKU_DEVICE_NAME            "STM32N657"

/* GPIO ports are letters A..H and N..Q; the STM32N657 has no GPIOI..GPIOM.
 * No code on this port reads these flags.  tiku_gpio_geometry.h numbers each
 * port by its letter's position: /dev/gpio/6 is GPIOG, the port carrying the
 * Nucleo LEDs, and GPION..GPIOQ are 13..16. */
#define TIKU_DEVICE_HAS_PORT1       1   /* GPIOB */
#define TIKU_DEVICE_HAS_PORT2       1   /* GPIOC */
#define TIKU_DEVICE_HAS_PORT3       1   /* GPIOD */
#define TIKU_DEVICE_HAS_PORT4       1   /* GPIOE */
#define TIKU_DEVICE_HAS_PORT5       1   /* GPIOF */
#define TIKU_DEVICE_HAS_PORT6       1   /* GPIOG */
#define TIKU_DEVICE_HAS_PORT7       1   /* GPIOH */
#define TIKU_DEVICE_HAS_PORT8       1   /* no GPIOI on this part */
#define TIKU_DEVICE_HAS_PORT9       1   /* no GPIOJ on this part */
#define TIKU_DEVICE_HAS_PORTJ       0   /* MSP430 port J, absent on STM32 */

/* TIKU_DEVICE_XOSC_HZ is the HSE frequency ST documents for this family.
 * The clock tree runs from HSI through PLL1, and no code reads the HSE. */
#define TIKU_DEVICE_HAS_LFXT        0
#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_XOSC_HZ         48000000UL
#define TIKU_DEVICE_CS_HAS_KEY      0
#define TIKU_DEVICE_CS_TYPE_STM32N6 1
#define TIKU_DEVICE_MAX_STABLE_MHZ  600

/* The AXI SRAM array is one contiguous span from 0x34000000 to 0x343C0000,
 * 3.75 MB; an access above the top hangs the bus with no fault.  The boot ROM
 * loads the image into a 255 KB window part-way up it (0x34180400): code,
 * data and stack occupy that window, and the tier arena the 2 MB above it. */
#define TIKU_DEVICE_RAM_START       0x34000000UL
#define TIKU_DEVICE_RAM_SIZE        0x3C0000UL

/* The window the boot ROM copies the image into: code, .data, .bss, the
 * durable cells and the stack.  It is the SRAM region of stm32n657.ld. */
#define TIKU_DEVICE_IMAGE_WINDOW_START  0x34180400UL
#define TIKU_DEVICE_IMAGE_WINDOW_SIZE   0x3FC00UL

/* No internal NVM: TIKU_DEVICE_FRAM_* describe the memory-mapped window of
 * the 64 MB Macronix NOR on XSPI2, the durable store.  Code runs from SRAM,
 * so _etext lies outside the window and `free` reports no NVM in use. */
#define TIKU_DEVICE_FRAM_SIZE       0x04000000UL
#define TIKU_DEVICE_FRAM_START      0x70000000UL
#define TIKU_DEVICE_FRAM_END        0x73FFFFFFUL
#define TIKU_DEVICE_NVM_LABEL       "NOR"

#define TIKU_DEVICE_HAS_MPU         1

#define TIKU_BOARD_UART_BAUD        115200U

#endif /* TIKU_DEVICE_STM32N6_H_ */
