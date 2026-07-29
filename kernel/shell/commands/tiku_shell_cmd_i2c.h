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
 * `scan` probes 0x08..0x77 with a zero-length write and lists what ACKs;
 * `read <addr> <count>` prints hex bytes; `write <addr> <byte>...` sends up to
 * the argv limit.  The bus initialises lazily at 100 kHz on first use.
 */
void tiku_shell_cmd_i2c(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_I2C_H_ */
