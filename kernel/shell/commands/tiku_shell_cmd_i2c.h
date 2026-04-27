/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_i2c.h - "i2c" command: bus scan / read / write
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_I2C_H_
#define TIKU_SHELL_CMD_I2C_H_

#include <stdint.h>

/**
 * @brief "i2c" command -- master-mode bus operations.
 *
 * Subcommands (numeric arguments accept decimal or 0x-prefixed hex):
 *
 *   i2c scan
 *       Probe addresses 0x08..0x77 with a zero-length write and
 *       print the list of devices that ACK.  Mirrors the output of
 *       i2cdetect(8) but in compact one-line form.
 *
 *   i2c read <addr> <count>
 *       Read up to TIKU_SHELL_I2C_MAX_BYTES from <addr>.  The bytes
 *       are printed as space-separated hex.
 *
 *   i2c write <addr> <byte> [<byte> ...]
 *       Write 1..(argc-2) bytes to <addr>.  The byte count is
 *       capped by TIKU_SHELL_MAX_ARGS.
 *
 * The bus is initialised lazily at standard speed (100 kHz) on
 * first use; subsequent calls are no-ops at the arch layer.
 */
void tiku_shell_cmd_i2c(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_I2C_H_ */
