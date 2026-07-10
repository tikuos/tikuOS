/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_jobs.h - "jobs" command: list / delete scheduled jobs
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_JOBS_H_
#define TIKU_SHELL_CMD_JOBS_H_

#include <stdint.h>

/**
 * @brief "jobs" command — manage scheduled jobs.
 *
 * Usage:
 *   jobs              List all scheduled jobs
 *   jobs del <id>     Free the slot at @p id
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_jobs(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_JOBS_H_ */
