/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_usbhs.h - "usb" and "store" commands (RA8P1 USB-HS).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_USBHS_H_
#define TIKU_SHELL_CMD_USBHS_H_

#include <stdint.h>

/**
 * @brief Handle `usb ...`.  argv[0] is "usb".
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_usb(uint8_t argc, const char *argv[]);

/**
 * @brief Handle `store ...`.  argv[0] is "store".
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_store(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_USBHS_H_ */
