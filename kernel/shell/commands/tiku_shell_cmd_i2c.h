/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_i2c.h - "i2c" command: bus scan / read / write
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_I2C_H_
#define TIKU_SHELL_CMD_I2C_H_

#include <stdint.h>

/**
 * @brief "i2c" command -- master-mode bus operations.
 *
 * `scan` lists each address in 0x08..0x77 that answers tiku_i2c_probe();
 * `read <addr> <count>` prints hex bytes; `write <addr> <byte>...` sends up to
 * 16.  Every subcommand first initialises the bus at 100 kHz.
 *
 * @note On MSP430 the 8-token argv fits 5 write bytes; the parser drops any
 *       further tokens without an error.
 */
void tiku_shell_cmd_i2c(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_I2C_H_ */
