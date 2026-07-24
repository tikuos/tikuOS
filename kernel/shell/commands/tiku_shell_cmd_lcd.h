/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
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

/**
 * @brief "lcd" command handler — paint the board's segment LCD.
 *
 * Sub-commands: "info" (panel size + icon count), "clear",
 * "char <pos> <ch>", "puts <text...>", "putsr <text...>",
 * "putsa <pos> <text...>", "putu <decimal>", "puti <signed-decimal>",
 * "puth <hex> [digits]", "putf <int> <decimals>",
 * "icon <id> <on|off|toggle>" and "icons clear".  With no argument it
 * prints the usage list.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] selects the sub-command, argv[2..]
 *              carry its position, numeric value or text words
 */
void tiku_shell_cmd_lcd(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_LCD_H_ */
