/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - P4 CPU0 kernel-test and firmware profile selection.
 *
 * The P4X RAM profiles select 40 or 400 MHz. The Olimex and FireBeetle
 * kernel profiles select a 360 MHz clock. The FRAM constants describe the
 * 16 MiB flash at 0x40000000; images map at 0x40100000.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_DEVICE_SELECT_H_
#define TIKU_ESP32P4_DEVICE_SELECT_H_
#if !defined(PLATFORM_ESP32P4) || \
    (!defined(TIKU_BOARD_OLIMEX_ESP32P4_PC) && \
     !defined(TIKU_BOARD_FIREBEETLE2_ESP32P4) && \
     !defined(TIKU_BOARD_ESP32_P4X_FUNCTION_EV_BOARD))
#error "Select an ESP32-P4 board profile"
#endif
#if defined(TIKU_BOARD_ESP32_P4X_FUNCTION_EV_BOARD)
#if !defined(TIKU_MINIMAL) || (MAIN_CPU_FREQ != 40 && MAIN_CPU_FREQ != 400)
#error "The P4X RAM diagnostic requires 40 or 400 MHz"
#endif
#define TIKU_DEVICE_NAME "ESP32-P4 rev 3.2"
#define TIKU_DEVICE_RAM_USABLE (64UL * 1024UL)
#define TIKU_DEVICE_MAX_STABLE_MHZ 400
#else
#if !defined(TIKU_P4_KERNEL_TEST) && !defined(TIKU_P4_FIRMWARE)
#error "Select the P4 kernel-test or firmware profile"
#endif
#if MAIN_CPU_FREQ != 360
#error "The P4 kernel test profile requires 360 MHz"
#endif
#define TIKU_DEVICE_NAME "ESP32-P4 rev 1.3"
#define TIKU_DEVICE_RAM_USABLE (0x2bbd0UL + 384UL * 1024UL)
#define TIKU_DEVICE_MAX_STABLE_MHZ 360
#endif
#define TIKU_DEVICE_RAM_START 0x4ff00000UL
#define TIKU_DEVICE_RAM_SIZE (768UL * 1024UL)
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
#define TIKU_DEVICE_GPIO_COUNT 55
#if defined(TIKU_BOARD_ESP32_P4X_FUNCTION_EV_BOARD)
#include "boards/tiku_board_p4x_function_ev.h"
#elif defined(TIKU_BOARD_FIREBEETLE2_ESP32P4)
#include "boards/tiku_board_firebeetle2_esp32p4.h"
#else
#include "boards/tiku_board_olimex_esp32p4_pc.h"
#endif
#endif
