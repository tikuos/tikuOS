/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ipc.h - IPC endpoint status and bounded shared-stream test.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_CMD_IPC_H_
#define TIKU_SHELL_CMD_IPC_H_
#include <stdint.h>
/** @brief List endpoints, or run "ipc test <endpoint>" on an unused shared peer. */
void tiku_shell_cmd_ipc(uint8_t argc, const char *argv[]);
#endif
