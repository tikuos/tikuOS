/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_fram.h - "fram" command: the board's external FRAM parts
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_FRAM_H_
#define TIKU_SHELL_CMD_FRAM_H_

#include <stdint.h>

/**
 * @brief "fram" command -- identify, read and write the board's FRAM parts.
 *
 * `fram` lists the parts; `fram <spi|i2c> id` prints the device ID,
 * `read <addr> [count]` up to 16 bytes and `write <addr> <byte>...` writes up
 * to 16.  The part must be powered and its bus switched to this processor.
 */
void tiku_shell_cmd_fram(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_FRAM_H_ */
