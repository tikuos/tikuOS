/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_once.c - "once" command implementation
 *
 * Thin wrapper over tiku_shell_jobs_schedule_argv() that registers a
 * single-shot job.  All parsing, validation, and diagnostics live in
 * the jobs subsystem; this file simply selects the ONCE job type.
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
