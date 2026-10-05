/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
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

/**
 * @brief "freq" command handler — show or set the CPU core frequency.
 *
 * No argument prints the current core clock in MHz; `freq <mhz>` requests one
 * of the platform's rates (see the usage line) and reports what was applied.
 * `freq probe` dumps the clock tree or HP identity where the port has one.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] is the requested frequency in
 *              MHz (decimal) or the "probe" sub-command
 */
void tiku_shell_cmd_freq(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_FREQ_H_ */
