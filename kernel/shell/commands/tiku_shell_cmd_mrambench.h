/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mrambench.h - "mrambench" command: time the MRAM programmer
 *
 * Ambiq-only.  Benchmarks the on-chip bootrom MRAM programmer
 * (nv_program_main2) at several span sizes so the fixed per-call overhead
 * can be separated from the per-word cost -- the numbers that size a future
 * block-granular delta flush.  Non-destructive: it programs scratch in the
 * upper half of the reserved mirror page.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_MRAMBENCH_H_
#define TIKU_SHELL_CMD_MRAMBENCH_H_

#include <stdint.h>

/**
 * @brief "mrambench" command handler — time the bootrom MRAM programmer.
 *
 * Usage: mrambench
 * Prints a table of span size -> best-of-N program cycles/us, plus a
 * two-point fit of per-call overhead vs per-word cost.
 *
 * @param argc  Argument count (unused)
 * @param argv  Argument vector (unused)
 */
void tiku_shell_cmd_mrambench(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_MRAMBENCH_H_ */
