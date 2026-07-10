/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_resume.h - "resume" command: resume a stopped process
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_RESUME_H_
#define TIKU_SHELL_CMD_RESUME_H_

#include <stdint.h>

/**
 * @brief "resume" command handler — resume a stopped process by PID.
 *
 * Usage: resume <pid>
 * The PID corresponds to the index shown by the "ps" command.
 *
 * @param argc  Argument count
 * @param argv  Argument vector (argv[1] = PID)
 */
void tiku_shell_cmd_resume(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_RESUME_H_ */
