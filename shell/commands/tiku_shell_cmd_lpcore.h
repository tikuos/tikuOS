/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_lpcore.h - "lpcore" command (ESP32-P4 LP core).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_LPCORE_H_
#define TIKU_SHELL_CMD_LPCORE_H_

#include <stdint.h>

/**
 * @brief Print the LP core's state, its control registers and the payload's
 *        fault record, or run a payload command: led, i2c, clock, wake.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @note Defined only when TIKU_SHELL_CMD_LPCORE is set.
 */
void tiku_shell_cmd_lpcore(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_LPCORE_H_ */
