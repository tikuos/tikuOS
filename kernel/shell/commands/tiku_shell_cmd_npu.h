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
 * @brief Handle `npu [off | bench [rounds] | load <name>]`.
 *
 * `bench` times the model on the NPU and the M85 (20 rounds by default),
 * `load` loads a model from /data and `off` gates the NPU.  Anything else
 * brings the NPU up and prints its id, MAC rate, SHRAM size and model.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_npu(uint8_t argc, const char *argv[]);

/**
 * @brief Handle `npu-test [seed] [rounds]`: check NPU output against the M85.
 *
 * Runs the self-test `rounds` times (4 by default) from `seed` (1 by default)
 * and returns at a failed round; after a full pass it runs the tampered,
 * no-irq, bad-weight and no-maintenance variants, each expected to fail.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_npu_test(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_NPU_H_ */
