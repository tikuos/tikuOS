/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_nvmprobe.h - "nvmprobe" command (carved NVM region diagnostic)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_NVMPROBE_H_
#define TIKU_SHELL_CMD_NVMPROBE_H_

#include <stdint.h>

/** @brief "nvmprobe" -- exercise the carved NVM region backend (substrate B). */
void tiku_shell_cmd_nvmprobe(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_NVMPROBE_H_ */
