/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_reclaim.h - "mem reclaim" command: backing reconstruction
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_CMD_RECLAIM_H_
#define TIKU_SHELL_CMD_RECLAIM_H_
#include <stdint.h>

/**
 * @brief "mem" command handler: `mem reclaim [status | last | pending |
 *        owners | stats | mode [off|reactive] | retry 1:GEN | cancel 1:GEN]`.
 *
 * With no entry it prints the current job.  mode, retry and cancel with a
 * value are refused at once or accepted; an accepted one can complete later,
 * and `status` shows the outcome.
 *
 * @note Defined only when TIKU_MEM_RECLAIM_ENABLE is set.
 */
void tiku_shell_cmd_reclaim(uint8_t argc, const char *argv[]);
#endif
