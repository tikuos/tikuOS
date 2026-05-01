/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_lcd.h - "lcd" shell command (segment-LCD harness)
 *
 * Drives the platform-independent tiku_lcd interface from the shell
 * so user-in-the-loop tests (TikuBench/lcd_test.py) can paint frames
 * and ask the operator what they actually see.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_LCD_H_
#define TIKU_SHELL_CMD_LCD_H_

#include <stdint.h>

void tiku_shell_cmd_lcd(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_LCD_H_ */
