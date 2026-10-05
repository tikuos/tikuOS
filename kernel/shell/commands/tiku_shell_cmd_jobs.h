/*
 * Tiku Operating System v0.06
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
 * `jobs` lists every scheduled job, `jobs del <id>` frees the slot at that
 * id, and `jobs del all` frees every slot.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_jobs(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_JOBS_H_ */
