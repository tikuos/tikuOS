/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mrambench.h - "mrambench" command: time the MRAM programmer.
 *
 * Benchmarks the bootrom programmer at several span sizes so the fixed
 * per-call overhead separates from the per-word cost.  Non-destructive: it
 * programs scratch in the upper half of the reserved mirror page.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_MRAMBENCH_H_
#define TIKU_SHELL_CMD_MRAMBENCH_H_

#include <stdint.h>

/**
 * @brief "mrambench" command handler — time the bootrom MRAM programmer.
 *
 * Usage: mrambench [verify]
 * Prints best-of-4 program cycles and microseconds per span, and a fit of
 * per-call overhead vs per-word cost; `verify` checks the flush dirty-check.
 *
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] may be "verify"
 */
void tiku_shell_cmd_mrambench(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_MRAMBENCH_H_ */
