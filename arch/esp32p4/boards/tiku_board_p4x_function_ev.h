/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_p4x_function_ev.h - display and console pins for the P4X
 * Function EV Board and its 1024 by 600 touchscreen.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BOARD_P4X_FUNCTION_EV_H_
#define TIKU_BOARD_P4X_FUNCTION_EV_H_

#define TIKU_BOARD_NAME "Espressif ESP32-P4X-Function-EV-Board"
#define TIKU_BOARD_I2C_SDA_PIN 7u
#define TIKU_BOARD_I2C_SCL_PIN 8u
#define TIKU_BOARD_I2C_BRW_100K 400u
#define TIKU_BOARD_USB_DM_PIN 24u
#define TIKU_BOARD_USB_DP_PIN 25u
#define TIKU_BOARD_I2S_MCLK_PIN 13u
#define TIKU_BOARD_I2S_BCLK_PIN 12u
#define TIKU_BOARD_I2S_WS_PIN   10u
#define TIKU_BOARD_I2S_DIN_PIN  11u
#define TIKU_BOARD_LCD_BL_PIN 26u
#define TIKU_BOARD_LCD_RESET_PIN 27u
/* I2S on 10..13, USB on 24..25, panel on 26..27 and the button on 35. */
#define TIKU_BOARD_GPIO_RESERVED ((15ULL << 10) | (15ULL << 24) | (1ULL << 35))
#endif
