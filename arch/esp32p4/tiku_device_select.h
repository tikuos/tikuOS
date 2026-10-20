/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - P4 CPU0 kernel-test and firmware profile selection.
 *
 * Refuses a build without the Olimex ESP32-P4-PC or FireBeetle 2 board, one
 * of those profiles and MAIN_CPU_FREQ 360.  The FRAM constants describe the
 * 16 MiB flash at 0x40000000; images map at 0x40100000.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_DEVICE_SELECT_H_
#define TIKU_ESP32P4_DEVICE_SELECT_H_
#if !defined(PLATFORM_ESP32P4) || \
    (!defined(TIKU_BOARD_OLIMEX_ESP32P4_PC) && \
     !defined(TIKU_BOARD_FIREBEETLE2_ESP32P4))
#error "ESP32-P4 requires the Olimex ESP32-P4-PC or FireBeetle 2 board profile"
#endif
#if !defined(TIKU_P4_KERNEL_TEST) && !defined(TIKU_P4_FIRMWARE)
#error "Select the P4 kernel-test or firmware profile"
#endif
#if MAIN_CPU_FREQ != 360
#error "The P4 kernel test profile requires 360 MHz"
#endif
#define TIKU_DEVICE_NAME "ESP32-P4 rev 1.3"
#if defined(TIKU_BOARD_FIREBEETLE2_ESP32P4)
#define TIKU_BOARD_NAME "DFRobot FireBeetle 2 ESP32-P4"
#else
#define TIKU_BOARD_NAME "Olimex ESP32-P4-PC"
#endif
#define TIKU_DEVICE_RAM_START 0x4ff00000UL
#define TIKU_DEVICE_RAM_SIZE (768UL * 1024UL)
#define TIKU_DEVICE_RAM_USABLE (0x2bbd0UL + 384UL * 1024UL)
#define TIKU_DEVICE_IMAGE_WINDOW_START 0x40100000UL
#define TIKU_DEVICE_IMAGE_WINDOW_SIZE (3UL * 1024UL * 1024UL)
#define TIKU_DEVICE_FRAM_START 0x40000000UL
#define TIKU_DEVICE_FRAM_SIZE (16UL * 1024UL * 1024UL)
#define TIKU_DEVICE_FRAM_END 0x40ffffffUL
#define TIKU_DEVICE_NVM_LABEL "Flash"
#define TIKU_DEVICE_FRAM_CONFIG_SIZE 576u
#define TIKU_DEVICE_HAS_LFXT 0
#define TIKU_DEVICE_HAS_HFXT 1
#define TIKU_DEVICE_XOSC_HZ 40000000UL
#define TIKU_DEVICE_MAX_STABLE_MHZ 360
#define TIKU_DEVICE_GPIO_COUNT 55
#endif
