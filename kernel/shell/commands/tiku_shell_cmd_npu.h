/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_npu.h - "npu" shell command.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_NPU_H_
#define TIKU_SHELL_CMD_NPU_H_

#include <stdint.h>

#include <kernel/shell/tiku_shell_config.h>

/**
 * @brief Handle `npu [off]`; with no argument it releases and reports.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_npu(uint8_t argc, const char *argv[]);

/**
 * @brief Handle `npu-test [seed] [rounds]`: run the stream and check it.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_npu_test(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_NPU_H_ */
