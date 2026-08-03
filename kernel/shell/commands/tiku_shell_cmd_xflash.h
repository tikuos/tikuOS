/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_xflash.h - "xflash" command: external NOR over XSPI.
 *
 * Identity, read, and an erase/program/verify round trip against a scratch
 * sector at the top of the device.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_XFLASH_H_
#define TIKU_SHELL_CMD_XFLASH_H_

#include <stdint.h>

/**
 * @brief "xflash" command handler.
 *
 * @param argc  Argument count including the command word
 * @param argv  Argument vector
 */
void tiku_shell_cmd_xflash(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_XFLASH_H_ */
