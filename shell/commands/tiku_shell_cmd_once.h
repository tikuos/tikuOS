/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_once.h - "once" command: schedule one-shot command
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_ONCE_H_
#define TIKU_SHELL_CMD_ONCE_H_

#include <stdint.h>

/**
 * @brief "once" command: `once <seconds> <command...>` runs a command once.
 *
 * Joins argv[2..] with single spaces and schedules it as a one-shot job due
 * <seconds> (1 to 65535) from now; the job's slot is freed when it fires.
 * Success prints nothing, and `jobs` lists the job.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_once(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ONCE_H_ */
