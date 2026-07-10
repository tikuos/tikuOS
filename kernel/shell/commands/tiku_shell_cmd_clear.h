/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_clear.h - "clear" command: ANSI clear screen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CLEAR_H_
#define TIKU_SHELL_CMD_CLEAR_H_

#include <stdint.h>

/**
 * @brief "clear" command -- emit ANSI CSI "2J H" to wipe the
 * terminal and place the cursor at the top-left.
 *
 * Backends without an ANSI-compatible viewer (raw log file, an
 * LLM channel that captures bytes verbatim) will simply receive
 * the literal escape bytes; the prompt redraws on the next line
 * either way.
 */
void tiku_shell_cmd_clear(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CLEAR_H_ */
