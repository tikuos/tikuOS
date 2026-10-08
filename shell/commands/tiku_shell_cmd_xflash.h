/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_xflash.h - "xflash" command: external NOR over XSPI.
 *
 * Identity, a 16-byte dump at an address, an erase/program/verify round trip
 * on a scratch sector near the top of the device, and `write`, which programs
 * an image received over the console.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_XFLASH_H_
#define TIKU_SHELL_CMD_XFLASH_H_

#include <stdint.h>

/**
 * @brief "xflash" command handler:
 *        `xflash [id | test | dump <hexaddr> | write <hexaddr> <hexlen>]`.
 *
 * @param argc  Argument count including the command word
 * @param argv  Argument vector
 */
void tiku_shell_cmd_xflash(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_XFLASH_H_ */
