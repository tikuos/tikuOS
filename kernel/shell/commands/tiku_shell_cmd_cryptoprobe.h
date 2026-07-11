/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cryptoprobe.h - CRACEN bring-up probe (opt-in).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CRYPTOPROBE_H_
#define TIKU_SHELL_CMD_CRYPTOPROBE_H_

#include <kernel/shell/tiku_shell_config.h>

#if TIKU_SHELL_CMD_CRYPTOPROBE
void tiku_shell_cmd_cryptoprobe(int argc, char **argv);
#endif

#endif /* TIKU_SHELL_CMD_CRYPTOPROBE_H_ */
