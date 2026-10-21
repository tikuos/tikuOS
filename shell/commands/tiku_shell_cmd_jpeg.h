/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_jpeg.h - "jpeg" command: the ESP32-P4 JPEG codec and PPA
 * on a test pattern.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_CMD_JPEG_H_
#define TIKU_SHELL_CMD_JPEG_H_

#include <stdint.h>                          /* uint8_t in the handler type */

/**
 * @brief "jpeg" command handler: `jpeg pattern <w> <h> [rgb888|rgb565|grey]
 *        [444|422|420] [quality] [turn <0-3>] [scale <sixteenths>]`, then
 *        `jpeg dump` (the file in base64) or `jpeg check` (decoded back).
 *
 * @note The signature must match tiku_shell_handler_t (tiku_shell.h): the
 *       command table stores it.
 */
void tiku_shell_cmd_jpeg(uint8_t argc, const char *argv[]);
#endif /* TIKU_SHELL_CMD_JPEG_H_ */
