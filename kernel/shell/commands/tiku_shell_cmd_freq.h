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
 * With no argument it prints the current core clock in MHz.
 * "freq <mhz>" requests a core frequency (96, or turbo: 192 on Apollo4,
 * 250 on Apollo510) and reports the clock actually applied.  On
 * Apollo510, "freq probe" dumps the read-only silicon, trim and power
 * identity that decides whether High-Performance mode can be used.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] is the requested frequency in
 *              MHz (decimal) or the "probe" sub-command
 */
void tiku_shell_cmd_freq(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_FREQ_H_ */
