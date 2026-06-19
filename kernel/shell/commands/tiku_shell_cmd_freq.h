/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_freq.h - "freq" command: show / set the CPU core frequency.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_FREQ_H_
#define TIKU_SHELL_CMD_FREQ_H_

#include <stdint.h>

void tiku_shell_cmd_freq(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_FREQ_H_ */
