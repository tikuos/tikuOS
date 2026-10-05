/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_name.h - "name" command: read or set the device name
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_NAME_H_
#define TIKU_SHELL_CMD_NAME_H_

#include <stdint.h>

/**
 * @brief "name" command handler: read or set the persistent device name.
 *
 * Wraps /sys/device/name.  With no argument it prints the name; with one it
 * stores argv[1] in durable memory.  A name over 31 characters is cut to 31,
 * or refused when a configuration journal (TIKU_VFS_CONFIG_ENABLE) is in use.
 */
void tiku_shell_cmd_name(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_NAME_H_ */
