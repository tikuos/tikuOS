/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_every.c - "every" command implementation
 *
 * Passes its arguments to tiku_shell_jobs_schedule_argv() as a
 * TIKU_SHELL_JOB_EVERY job; that function parses them, prints any error and
 * fills a job slot.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_every.h"
#include <kernel/shell/tiku_shell_jobs.h>

void
tiku_shell_cmd_every(uint8_t argc, const char *argv[])
{
    (void)tiku_shell_jobs_schedule_argv(TIKU_SHELL_JOB_EVERY, argc, argv);
}
