/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_usbmsc.h - "usbmsc" command: present the board as a disk.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_CMD_USBMSC_H_
#define TIKU_SHELL_CMD_USBMSC_H_

#include <stdint.h>                          /* uint8_t in the handler type */

/**
 * @brief "usbmsc" command handler: `usbmsc up|down|stats|peek <lba>`.
 *
 * `up` brings the USB-HS core up and presents the disk, `down` stops it,
 * `stats` prints the MSC counters and `peek` dumps 16 bytes of a block.
 *
 * @note Defined only when TIKU_USBHS_MSC is set.  The signature must match
 *       tiku_shell_handler_t (tiku_shell.h): the command table stores it.
 */
void tiku_shell_cmd_usbmsc(uint8_t argc, const char *argv[]);
#endif /* TIKU_SHELL_CMD_USBMSC_H_ */
