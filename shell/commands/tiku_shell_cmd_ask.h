/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ask.h - "ask" command: put a question to the agents
 * subscribed to /sys/mcp/ask/question, or show where the last one stands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_ASK_H_
#define TIKU_SHELL_CMD_ASK_H_

#include <stdint.h>

/**
 * @brief "ask" command handler: `ask <question...>` asks and returns at
 *        once; `ask` alone prints the question, its state and its answer.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_ask(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ASK_H_ */
