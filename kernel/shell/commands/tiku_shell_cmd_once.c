/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_once.c - "once" command implementation
 *
 * Passes its arguments to tiku_shell_jobs_schedule_argv() with
 * TIKU_SHELL_JOB_ONCE; the jobs subsystem parses them and prints any error.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_once.h"
#include <kernel/shell/tiku_shell_jobs.h>

void
tiku_shell_cmd_once(uint8_t argc, const char *argv[])
{
    (void)tiku_shell_jobs_schedule_argv(TIKU_SHELL_JOB_ONCE, argc, argv);
}
