/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_name.h - "name" command: read or set the device name
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_NAME_H_
#define TIKU_SHELL_CMD_NAME_H_

#include <stdint.h>

/**
 * @brief "name" command handler — read or set the persistent
 *        device name.
 *
 * Usage:
 *   name                  — print the current device name
 *   name <new-name>       — set the device name (FRAM-backed,
 *                           survives reboot, max 31 chars)
 *
 * Thin wrapper over /sys/device/name. The name is intended for
 * use as an mDNS hostname or any user-visible identifier.
 */
void tiku_shell_cmd_name(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_NAME_H_ */
