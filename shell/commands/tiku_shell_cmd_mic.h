/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mic.h - "mic" command: the FireBeetle 2's PDM microphone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_CMD_MIC_H_
#define TIKU_SHELL_CMD_MIC_H_

#include <stdint.h>                          /* uint8_t in the handler type */

/**
 * @brief "mic" command handler: `mic start [rate] [left|right|both]`,
 *        `mic stop`, `mic level`, `mic stats`, `mic dump <samples>`.
 *
 * @note The signature must match tiku_shell_handler_t (tiku_shell.h): the
 *       command table stores it.
 */
void tiku_shell_cmd_mic(uint8_t argc, const char *argv[]);
#endif /* TIKU_SHELL_CMD_MIC_H_ */
