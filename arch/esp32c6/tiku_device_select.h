/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_device_select.h - Native C6 board LED and UART bindings.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_DEVICE_SELECT_H
#define TIKU_C6_DEVICE_SELECT_H
#include "tiku.h"
#include <interfaces/led/tiku_led.h>
#include "tiku_uart_arch.h"
#define TIKU_BOARD_LED1_INIT() tiku_led_init(0)
#define TIKU_BOARD_LED2_INIT() tiku_led_init(1)
#define TIKU_BOARD_LED1_ON() tiku_led_on(0)
#define TIKU_BOARD_LED2_ON() tiku_led_on(1)
#define TIKU_BOARD_LED1_OFF() tiku_led_off(0)
#define TIKU_BOARD_LED2_OFF() tiku_led_off(1)
#define TIKU_BOARD_LED1_TOGGLE() tiku_led_toggle(0)
#define TIKU_BOARD_LED2_TOGGLE() tiku_led_toggle(1)
#endif
