/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mrambench.h - "mrambench" command: time the MRAM programmer.
 *
 * Times the bootrom MRAM programmer at each span that fits the upper half of
 * the MRAM mirror region and derives a per-call overhead and a per-word cost;
 * the timing runs only when the live image fits in the lower half.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_MRAMBENCH_H_
#define TIKU_SHELL_CMD_MRAMBENCH_H_

#include <stdint.h>

/**
 * @brief "mrambench" command handler: time the bootrom MRAM programmer.
 *
 * Usage: mrambench [verify]
 * Prints the best-of-4 cycles and microseconds per span and a two-point fit of
 * per-call overhead and per-word cost.  `verify` runs the flush self-test.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] may be "verify"
 */
void tiku_shell_cmd_mrambench(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_MRAMBENCH_H_ */
